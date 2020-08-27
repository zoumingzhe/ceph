=======================
CephFS Exports over NFS
=======================

CephFS namespaces can be exported over NFS protocol using the
`NFS-Ganesha NFS server <https://github.com/nfs-ganesha/nfs-ganesha/wiki>`_.

Requirements
============

-  Latest Ceph file system with mgr enabled
-  'nfs-ganesha', 'nfs-ganesha-ceph', 'nfs-ganesha-rados-grace' and
   'nfs-ganesha-rados-urls' packages (version 3.3 and above)

Create NFS Ganesha Cluster
==========================

.. code:: bash

    $ ceph nfs cluster create <type=cephfs> <clusterid> [<placement>]

This creates a common recovery pool for all Ganesha daemons, new user based on
cluster_id and common ganesha config rados object.

Here type is export type and placement specifies the size of cluster and hosts.
For more details on placement specification refer the `orchestrator doc
<https://docs.ceph.com/docs/master/mgr/orchestrator/#placement-specification>`_.
Currently only CephFS export type is supported.

Update NFS Ganesha Cluster
==========================

.. code:: bash

    $ ceph nfs cluster update <clusterid> <placement>

This updates the deployed cluster according to the placement value.

Delete NFS Ganesha Cluster
==========================

.. code:: bash

    $ ceph nfs cluster delete <clusterid>

This deletes the deployed cluster.

List NFS Ganesha Cluster
========================

.. code:: bash

    $ ceph nfs cluster ls

This lists deployed clusters.

Show NFS Ganesha Cluster Information
====================================

.. code:: bash

    $ ceph nfs cluster info [<clusterid>]

This displays ip and port of deployed cluster.

Set Customized Ganesha Configuration
====================================

.. code:: bash

    $ ceph nfs cluster config set <clusterid> -i <config_file>

With this the nfs cluster will use the specified config and it will have
precedence over default config blocks.

Reset Ganesha Configuration
===========================

.. code:: bash

    $ ceph nfs cluster config reset <clusterid>

This removes the user defined configuration.

Create CephFS Export
====================

.. code:: bash

    $ ceph nfs export create cephfs <fsname> <clusterid> <binding> [--readonly] [--path=/path/in/cephfs]

It creates export rados objects containing the export block. Here binding is
the pseudo root name and type is export type.

Delete CephFS Export
====================

.. code:: bash

    $ ceph nfs export delete <clusterid> <binding>

It deletes an export in cluster based on pseudo root name (binding).

List CephFS Export
==================

.. code:: bash

    $ ceph nfs export ls <clusterid> [--detailed]

It lists export for a cluster. With detailed option enabled it shows entire
export block.

Get CephFS Export
=================

.. code:: bash

    $ ceph nfs export get <clusterid> <binding>

It displays export block for a cluster based on pseudo root name (binding).

Configuring NFS-Ganesha to export CephFS with vstart
====================================================

1) Using cephadm

    .. code:: bash

        $ MDS=1 MON=1 OSD=3 NFS=1 ../src/vstart.sh -n -d --cephadm

    It can deploy only single ganesha daemon with vstart on default ganesha port.

2) Using test orchestrator

    .. code:: bash

       $ MDS=1 MON=1 OSD=3 NFS=1 ../src/vstart.sh -n -d

    It can deploy multiple ganesha daemons on random port. But this requires
    ganesha packages to be installed.

NFS: It is the number of NFS-Ganesha clusters to be created.

Mount
=====

After the exports are successfully created and Ganesha daemons are no longer in
grace period. The exports can be mounted by

.. code:: bash

    $ mount -t nfs -o port=<ganesha-port> <ganesha-host-name>:<ganesha-pseudo-path> <mount-point>

.. note:: Only NFS v4.0+ is supported.
