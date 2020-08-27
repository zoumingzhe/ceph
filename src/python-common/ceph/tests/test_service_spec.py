# flake8: noqa
import json
import yaml

import pytest

from ceph.deployment.service_spec import HostPlacementSpec, PlacementSpec, \
    ServiceSpec, ServiceSpecValidationError, RGWSpec, NFSServiceSpec, IscsiServiceSpec
from ceph.deployment.drive_group import DriveGroupSpec


@pytest.mark.parametrize("test_input,expected, require_network",
                         [("myhost", ('myhost', '', ''), False),
                          ("myhost=sname", ('myhost', '', 'sname'), False),
                          ("myhost:10.1.1.10", ('myhost', '10.1.1.10', ''), True),
                          ("myhost:10.1.1.10=sname", ('myhost', '10.1.1.10', 'sname'), True),
                          ("myhost:10.1.1.0/32", ('myhost', '10.1.1.0/32', ''), True),
                          ("myhost:10.1.1.0/32=sname", ('myhost', '10.1.1.0/32', 'sname'), True),
                          ("myhost:[v1:10.1.1.10:6789]", ('myhost', '[v1:10.1.1.10:6789]', ''), True),
                          ("myhost:[v1:10.1.1.10:6789]=sname", ('myhost', '[v1:10.1.1.10:6789]', 'sname'), True),
                          ("myhost:[v1:10.1.1.10:6789,v2:10.1.1.11:3000]", ('myhost', '[v1:10.1.1.10:6789,v2:10.1.1.11:3000]', ''), True),
                          ("myhost:[v1:10.1.1.10:6789,v2:10.1.1.11:3000]=sname", ('myhost', '[v1:10.1.1.10:6789,v2:10.1.1.11:3000]', 'sname'), True),
                          ])
def test_parse_host_placement_specs(test_input, expected, require_network):
    ret = HostPlacementSpec.parse(test_input, require_network=require_network)
    assert ret == expected
    assert str(ret) == test_input


@pytest.mark.parametrize(
    "test_input,expected",
    [
        ('', "PlacementSpec()"),
        ("count:2", "PlacementSpec(count=2)"),
        ("3", "PlacementSpec(count=3)"),
        ("host1 host2", "PlacementSpec(hosts=[HostPlacementSpec(hostname='host1', network='', name=''), HostPlacementSpec(hostname='host2', network='', name='')])"),
        ("host1;host2", "PlacementSpec(hosts=[HostPlacementSpec(hostname='host1', network='', name=''), HostPlacementSpec(hostname='host2', network='', name='')])"),
        ("host1,host2", "PlacementSpec(hosts=[HostPlacementSpec(hostname='host1', network='', name=''), HostPlacementSpec(hostname='host2', network='', name='')])"),
        ("host1 host2=b", "PlacementSpec(hosts=[HostPlacementSpec(hostname='host1', network='', name=''), HostPlacementSpec(hostname='host2', network='', name='b')])"),
        ("host1=a host2=b", "PlacementSpec(hosts=[HostPlacementSpec(hostname='host1', network='', name='a'), HostPlacementSpec(hostname='host2', network='', name='b')])"),
        ("host1:1.2.3.4=a host2:1.2.3.5=b", "PlacementSpec(hosts=[HostPlacementSpec(hostname='host1', network='1.2.3.4', name='a'), HostPlacementSpec(hostname='host2', network='1.2.3.5', name='b')])"),
        ("myhost:[v1:10.1.1.10:6789]", "PlacementSpec(hosts=[HostPlacementSpec(hostname='myhost', network='[v1:10.1.1.10:6789]', name='')])"),
        ('2 host1 host2', "PlacementSpec(count=2, hosts=[HostPlacementSpec(hostname='host1', network='', name=''), HostPlacementSpec(hostname='host2', network='', name='')])"),
        ('label:foo', "PlacementSpec(label='foo')"),
        ('3 label:foo', "PlacementSpec(count=3, label='foo')"),
        ('*', "PlacementSpec(host_pattern='*')"),
        ('3 data[1-3]', "PlacementSpec(count=3, host_pattern='data[1-3]')"),
        ('3 data?', "PlacementSpec(count=3, host_pattern='data?')"),
        ('3 data*', "PlacementSpec(count=3, host_pattern='data*')"),
    ])
def test_parse_placement_specs(test_input, expected):
    ret = PlacementSpec.from_string(test_input)
    assert str(ret) == expected

@pytest.mark.parametrize(
    "test_input",
    [
        ("host=a host*"),
        ("host=a label:wrong"),
        ("host? host*"),
    ]
)
def test_parse_placement_specs_raises(test_input):
    with pytest.raises(ServiceSpecValidationError):
        PlacementSpec.from_string(test_input)

@pytest.mark.parametrize("test_input",
                         # wrong subnet
                         [("myhost:1.1.1.1/24"),
                          # wrong ip format
                          ("myhost:1"),
                          ])
def test_parse_host_placement_specs_raises_wrong_format(test_input):
    with pytest.raises(ValueError):
        HostPlacementSpec.parse(test_input)


def _get_dict_spec(s_type, s_id):
    dict_spec = {
        "service_id": s_id,
        "service_type": s_type,
        "placement":
            dict(hosts=["host1:1.1.1.1"])
    }
    if s_type == 'nfs':
        dict_spec['pool'] = 'pool'
    elif s_type == 'iscsi':
        dict_spec['pool'] = 'pool'
        dict_spec['api_user'] = 'api_user'
        dict_spec['api_password'] = 'api_password'
    elif s_type == 'osd':
        dict_spec['spec'] = {
            'data_devices': {
                'all': True
            }
        }
    elif s_type == 'rgw':
        dict_spec['rgw_realm'] = 'realm'
        dict_spec['rgw_zone'] = 'zone'

    return dict_spec


@pytest.mark.parametrize(
    "s_type,o_spec,s_id",
    [
        ("mgr", ServiceSpec, 'test'),
        ("mon", ServiceSpec, 'test'),
        ("mds", ServiceSpec, 'test'),
        ("rgw", RGWSpec, 'realm.zone'),
        ("nfs", NFSServiceSpec, 'test'),
        ("iscsi", IscsiServiceSpec, 'test'),
        ("osd", DriveGroupSpec, 'test'),
    ])
def test_servicespec_map_test(s_type, o_spec, s_id):
    spec = ServiceSpec.from_json(_get_dict_spec(s_type, s_id))
    assert isinstance(spec, o_spec)
    assert isinstance(spec.placement, PlacementSpec)
    assert isinstance(spec.placement.hosts[0], HostPlacementSpec)
    assert spec.placement.hosts[0].hostname == 'host1'
    assert spec.placement.hosts[0].network == '1.1.1.1'
    assert spec.placement.hosts[0].name == ''
    assert spec.validate() is None
    ServiceSpec.from_json(spec.to_json())


def test_yaml():
    y = """service_type: crash
service_name: crash
placement:
  host_pattern: '*'
---
service_type: crash
service_name: crash
placement:
  host_pattern: '*'
unmanaged: true
---
service_type: rgw
service_id: default-rgw-realm.eu-central-1.1
service_name: rgw.default-rgw-realm.eu-central-1.1
placement:
  hosts:
  - hostname: ceph-001
    name: ''
    network: ''
spec:
  rgw_realm: default-rgw-realm
  rgw_zone: eu-central-1
  subcluster: '1'
---
service_type: osd
service_id: osd_spec_default
service_name: osd.osd_spec_default
placement:
  host_pattern: '*'
spec:
  data_devices:
    model: MC-55-44-XZ
  db_devices:
    model: SSD-123-foo
  filter_logic: AND
  objectstore: bluestore
  wal_devices:
    model: NVME-QQQQ-987
"""

    for y in y.split('---\n'):
        data = yaml.safe_load(y)
        object = ServiceSpec.from_json(data)

        assert yaml.dump(object) == y
        assert yaml.dump(ServiceSpec.from_json(object.to_json())) == y

@pytest.mark.parametrize("spec1, spec2, eq",
                         [
                             (
                                     ServiceSpec(
                                         service_type='mon'
                                     ),
                                     ServiceSpec(
                                         service_type='mon'
                                     ),
                                     True
                             ),
                             (
                                     ServiceSpec(
                                         service_type='mon'
                                     ),
                                     ServiceSpec(
                                         service_type='mon',
                                         service_id='foo'
                                     ),
                                     True
                             ),
                             # Add service_type='mgr'
                             (
                                     ServiceSpec(
                                         service_type='osd'
                                     ),
                                     ServiceSpec(
                                         service_type='osd',
                                     ),
                                     True
                             ),
                             (
                                     ServiceSpec(
                                         service_type='osd'
                                     ),
                                     DriveGroupSpec(),
                                     True
                             ),
                             (
                                     ServiceSpec(
                                         service_type='osd'
                                     ),
                                     ServiceSpec(
                                         service_type='osd',
                                         service_id='foo',
                                     ),
                                     False
                             ),
                             (
                                     ServiceSpec(
                                         service_type='rgw'
                                     ),
                                     RGWSpec(),
                                     True
                             ),
                         ])
def test_spec_hash_eq(spec1: ServiceSpec,
                      spec2: ServiceSpec,
                      eq: bool):

    assert (spec1 == spec2) is eq

@pytest.mark.parametrize(
    "s_type,s_id,s_name",
    [
        ('mgr', 's_id', 'mgr'),
        ('mon', 's_id', 'mon'),
        ('mds', 's_id', 'mds.s_id'),
        ('rgw', 's_id', 'rgw.s_id'),
        ('nfs', 's_id', 'nfs.s_id'),
        ('iscsi', 's_id', 'iscsi.s_id'),
        ('osd', 's_id', 'osd.s_id'),
    ])
def test_service_name(s_type, s_id, s_name):
    spec = ServiceSpec.from_json(_get_dict_spec(s_type, s_id))
    spec.validate()
    assert spec.service_name() == s_name
