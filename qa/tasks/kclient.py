"""
Mount/unmount a ``kernel`` client.
"""
import contextlib
import logging

from teuthology.misc import deep_merge
from teuthology.orchestra.run import CommandFailedError
from teuthology import misc
from teuthology.contextutil import MaxWhileTries
from tasks.cephfs.kernel_mount import KernelMount

log = logging.getLogger(__name__)

@contextlib.contextmanager
def task(ctx, config):
    """
    Mount/unmount a ``kernel`` client.

    The config is optional and defaults to mounting on all clients. If
    a config is given, it is expected to be a list of clients to do
    this operation on. This lets you e.g. set up one client with
    ``ceph-fuse`` and another with ``kclient``.

    ``brxnet`` should be a Private IPv4 Address range, default range is
    [192.168.0.0/16]

    Example that mounts all clients::

        tasks:
        - ceph:
        - kclient:
        - interactive:
        - brxnet: [192.168.0.0/16]

    Example that uses both ``kclient` and ``ceph-fuse``::

        tasks:
        - ceph:
        - ceph-fuse: [client.0]
        - kclient: [client.1]
        - interactive:


    Pass a dictionary instead of lists to specify per-client config:

        tasks:
        -kclient:
            client.0:
                debug: true

    :param ctx: Context
    :param config: Configuration
    """
    log.info('Mounting kernel clients...')
    assert config is None or isinstance(config, list) or isinstance(config, dict), \
        "task kclient got invalid config"

    if config is None:
        config = ['client.{id}'.format(id=id_)
                  for id_ in misc.all_roles_of_type(ctx.cluster, 'client')]

    if isinstance(config, list):
        client_roles = config
        config = dict([r, dict()] for r in client_roles)
    elif isinstance(config, dict):
        client_roles = filter(lambda x: 'client.' in x, config.keys())
    else:
        raise ValueError("Invalid config object: {0} ({1})".format(config, config.__class__))

    # config has been converted to a dict by this point
    overrides = ctx.config.get('overrides', {})
    deep_merge(config, overrides.get('kclient', {}))

    clients = list(misc.get_clients(ctx=ctx, roles=client_roles))

    test_dir = misc.get_testdir(ctx)

    for id_, remote in clients:
        KernelMount.cleanup_stale_netnses_and_bridge(remote)

    mounts = {}
    for id_, remote in clients:
        client_config = config.get("client.%s" % id_)
        if client_config is None:
            client_config = {}

        if config.get("disabled", False) or not client_config.get('mounted', True):
            continue

        kernel_mount = KernelMount(
            ctx,
            test_dir,
            id_,
            remote,
            ctx.teuthology_config.get('brxnet', None),
        )

        mounts[id_] = kernel_mount

        if client_config.get('debug', False):
            remote.run(args=["sudo", "bash", "-c", "echo 'module ceph +p' > /sys/kernel/debug/dynamic_debug/control"])
            remote.run(args=["sudo", "bash", "-c", "echo 'module libceph +p' > /sys/kernel/debug/dynamic_debug/control"])

        kernel_mount.mount()


    def umount_all():
        log.info('Unmounting kernel clients...')

        forced = False
        for mount in mounts.values():
            if mount.is_mounted():
                try:
                    mount.umount()
                except (CommandFailedError, MaxWhileTries):
                    log.warning("Ordinary umount failed, forcing...")
                    forced = True
                    mount.umount_wait(force=True)

        for id_, remote in clients:
            KernelMount.cleanup_stale_netnses_and_bridge(remote)

        return forced

    ctx.mounts = mounts
    try:
        yield mounts
    except:
        umount_all()  # ignore forced retval, we are already in error handling
    finally:

        forced = umount_all()
        if forced:
            # The context managers within the kclient manager worked (i.e.
            # the test workload passed) but for some reason we couldn't
            # umount, so turn this into a test failure.
            raise RuntimeError("Kernel mounts did not umount cleanly")
