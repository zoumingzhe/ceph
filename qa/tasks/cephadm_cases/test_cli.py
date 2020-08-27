import logging

from tasks.mgr.mgr_test_case import MgrTestCase

log = logging.getLogger(__name__)


class TestCephadmCLI(MgrTestCase):
    def _cmd(self, *args):
        return self.mgr_cluster.mon_manager.raw_cluster_cmd(*args)

    def _orch_cmd(self, *args):
        return self._cmd("orch", *args)

    def setUp(self):
        super(TestCephadmCLI, self).setUp()

    def test_yaml(self):
        """
        to prevent oddities like

        >>> import yaml
        ... from collections import OrderedDict
        ... assert yaml.dump(OrderedDict()) == '!!python/object/apply:collections.OrderedDict\\n- []\\n'
        """
        out = self._orch_cmd('device', 'ls', '--format', 'yaml')
        self.assertNotIn('!!python', out)

        out = self._orch_cmd('host', 'ls', '--format', 'yaml')
        self.assertNotIn('!!python', out)

        out = self._orch_cmd('ls', '--format', 'yaml')
        self.assertNotIn('!!python', out)

        out = self._orch_cmd('ps', '--format', 'yaml')
        self.assertNotIn('!!python', out)

        out = self._orch_cmd('status', '--format', 'yaml')
        self.assertNotIn('!!python', out)

    def test_pause(self):
        self._orch_cmd('pause')
        self.wait_for_health('CEPHADM_PAUSED', 30)
        self._orch_cmd('resume')
        self.wait_for_health_clear(30)

    def test_daemon_restart(self):
        self._orch_cmd('daemon', 'stop', 'osd.0')
        self.wait_for_health('OSD_DOWN', 30)
        self._orch_cmd('daemon', 'start', 'osd.0')
        self.wait_for_health_clear(30)
        self._orch_cmd('daemon', 'restart', 'osd.0')
