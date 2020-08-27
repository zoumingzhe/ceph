import logging
from typing import TYPE_CHECKING, Dict, Optional, Tuple, Any, List, Set, cast

from ceph.deployment.service_spec import NFSServiceSpec
import rados

from orchestrator import OrchestratorError, DaemonDescription

from cephadm import utils
from cephadm.services.cephadmservice import CephadmService, CephadmDaemonSpec

if TYPE_CHECKING:
    from cephadm.module import CephadmOrchestrator

logger = logging.getLogger(__name__)


class NFSService(CephadmService):
    TYPE = 'nfs'

    def generate_config(self, daemon_spec: CephadmDaemonSpec) -> Tuple[Dict[str, Any], List[str]]:
        assert self.TYPE == daemon_spec.daemon_type

        daemon_type = daemon_spec.daemon_type
        daemon_id = daemon_spec.daemon_id
        host = daemon_spec.host

        deps: List[str] = []

        # find the matching NFSServiceSpec
        # TODO: find the spec and pass via _create_daemon instead ??
        dd = DaemonDescription()
        dd.daemon_type = daemon_type
        dd.daemon_id = daemon_id
        dd.hostname = host

        service_name = dd.service_name()
        specs = self.mgr.spec_store.find(service_name)

        if not specs:
            raise OrchestratorError('Cannot find service spec %s' % (service_name))
        elif len(specs) > 1:
            raise OrchestratorError('Found multiple service specs for %s' % (service_name))
        else:
            # cast to keep mypy happy
            spec = cast(NFSServiceSpec, specs[0])

        nfs = NFSGanesha(self.mgr, daemon_id, spec)

        # create the keyring
        entity = nfs.get_keyring_entity()
        keyring = nfs.get_or_create_keyring(entity=entity)

        # update the caps after get-or-create, the keyring might already exist!
        nfs.update_keyring_caps(entity=entity)

        # create the rados config object
        nfs.create_rados_config_obj()

        # generate the cephadm config
        cephadm_config = nfs.get_cephadm_config()
        cephadm_config.update(
            self.mgr._get_config_and_keyring(
                daemon_type, daemon_id,
                keyring=keyring,
                host=host
            )
        )

        return cephadm_config, deps

    def config(self, spec: NFSServiceSpec) -> None:
        assert self.TYPE == spec.service_type
        self.mgr._check_pool_exists(spec.pool, spec.service_name())

        logger.info('Saving service %s spec with placement %s' % (
            spec.service_name(), spec.placement.pretty_str()))
        self.mgr.spec_store.save(spec)

    def create(self, daemon_spec: CephadmDaemonSpec[NFSServiceSpec]) -> str:
        assert self.TYPE == daemon_spec.daemon_type
        assert daemon_spec.spec

        daemon_id = daemon_spec.daemon_id
        host = daemon_spec.host
        spec = daemon_spec.spec

        logger.info('Create daemon %s on host %s with spec %s' % (
            daemon_id, host, spec))
        return self.mgr._create_daemon(daemon_spec)

    def config_dashboard(self, daemon_descrs: List[DaemonDescription]):

        def get_set_cmd_dicts(out: str) -> List[dict]:
            locations: Set[str] = set()
            for dd in daemon_descrs:
                spec = cast(NFSServiceSpec,
                            self.mgr.spec_store.specs.get(dd.service_name(), None))
                if not spec or not spec.service_id:
                    logger.warning('No ServiceSpec or service_id found for %s', dd)
                    continue
                location = '{}:{}'.format(spec.service_id, spec.pool)
                if spec.namespace:
                    location = '{}/{}'.format(location, spec.namespace)
                locations.add(location)
            new_value = ','.join(locations)
            if new_value and new_value != out:
                return [{'prefix': 'dashboard set-ganesha-clusters-rados-pool-namespace',
                         'value': new_value}]
            return []

        self._check_and_set_dashboard(
            service_name='Ganesha',
            get_cmd='dashboard get-ganesha-clusters-rados-pool-namespace',
            get_set_cmd_dicts=get_set_cmd_dicts
        )


class NFSGanesha(object):
    def __init__(self,
                 mgr: "CephadmOrchestrator",
                 daemon_id: str,
                 spec: NFSServiceSpec) -> None:
        assert spec.service_id and daemon_id.startswith(spec.service_id)
        mgr._check_pool_exists(spec.pool, spec.service_name())

        self.mgr = mgr
        self.daemon_id = daemon_id
        self.spec = spec

    def get_daemon_name(self) -> str:
        return '%s.%s' % (self.spec.service_type, self.daemon_id)

    def get_rados_user(self) -> str:
        return '%s.%s' % (self.spec.service_type, self.daemon_id)

    def get_keyring_entity(self) -> str:
        return utils.name_to_config_section(self.get_rados_user())

    def get_or_create_keyring(self, entity: Optional[str] = None) -> str:
        if not entity:
            entity = self.get_keyring_entity()

        logger.info('Create keyring: %s' % entity)
        ret, keyring, err = self.mgr.mon_command({
            'prefix': 'auth get-or-create',
            'entity': entity,
        })

        if ret != 0:
            raise OrchestratorError(
                'Unable to create keyring %s: %s %s'
                % (entity, ret, err))
        return keyring

    def update_keyring_caps(self, entity: Optional[str] = None) -> None:
        if not entity:
            entity = self.get_keyring_entity()

        osd_caps = 'allow rw pool=%s' % (self.spec.pool)
        if self.spec.namespace:
            osd_caps = '%s namespace=%s' % (osd_caps, self.spec.namespace)

        logger.info('Updating keyring caps: %s' % entity)
        ret, out, err = self.mgr.mon_command({
            'prefix': 'auth caps',
            'entity': entity,
            'caps': ['mon', 'allow r',
                     'osd', osd_caps],
        })

        if ret != 0:
            raise OrchestratorError(
                'Unable to update keyring caps %s: %s %s'
                % (entity, ret, err))

    def create_rados_config_obj(self, clobber: Optional[bool] = False) -> None:
        with self.mgr.rados.open_ioctx(self.spec.pool) as ioctx:
            if self.spec.namespace:
                ioctx.set_namespace(self.spec.namespace)

            obj = self.spec.rados_config_name()
            exists = True
            try:
                ioctx.stat(obj)
            except rados.ObjectNotFound as e:
                exists = False

            if exists and not clobber:
                # Assume an existing config
                logger.info('Rados config object exists: %s' % obj)
            else:
                # Create an empty config object
                logger.info('Creating rados config object: %s' % obj)
                ioctx.write_full(obj, ''.encode('utf-8'))

    def get_ganesha_conf(self) -> str:
        context = dict(user=self.get_rados_user(),
                       nodeid=self.get_daemon_name(),
                       pool=self.spec.pool,
                       namespace=self.spec.namespace if self.spec.namespace else '',
                       url=self.spec.rados_config_location())
        return self.mgr.template.render('services/nfs/ganesha.conf.j2', context)

    def get_cephadm_config(self) -> Dict[str, Any]:
        config: Dict[str, Any] = {'pool': self.spec.pool}
        if self.spec.namespace:
            config['namespace'] = self.spec.namespace
        config['userid'] = self.get_rados_user()
        config['extra_args'] = ['-N', 'NIV_EVENT']
        config['files'] = {
            'ganesha.conf': self.get_ganesha_conf(),
        }
        logger.debug('Generated cephadm config-json: %s' % config)
        return config
