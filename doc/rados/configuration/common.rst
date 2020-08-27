
.. _ceph-conf-common-settings:

Common Settings
===============

The `Hardware Recommendations`_ section provides some hardware guidelines for
configuring a Ceph Storage Cluster. It is possible for a single :term:`Ceph
Node` to run multiple daemons. For example, a single node with multiple drives
may run one ``ceph-osd`` for each drive. Ideally, you will  have a node for a
particular type of process. For example, some nodes may run ``ceph-osd``
daemons, other nodes may run ``ceph-mds`` daemons, and still  other nodes may
run ``ceph-mon`` daemons.

Each node has a name identified by the ``host`` setting. Monitors also specify
a network address and port (i.e., domain name or IP address) identified by the
``addr`` setting.  A basic configuration file will typically specify only
minimal settings for each instance of monitor daemons. For example:

.. code-block:: ini

	[global]
	mon_initial_members = ceph1
	mon_host = 10.0.0.1


.. important:: The ``host`` setting is the short name of the node (i.e., not
   an fqdn). It is **NOT** an IP address either.  Enter ``hostname -s`` on
   the command line to retrieve the name of the node. Do not use ``host``
   settings for anything other than initial monitors unless you are deploying
   Ceph manually. You **MUST NOT** specify ``host`` under individual daemons
   when using deployment tools like ``chef`` or ``ceph-deploy``, as those tools
   will enter the appropriate values for you in the cluster map.


.. _ceph-network-config:

Networks
========

See the `Network Configuration Reference`_ for a detailed discussion about
configuring a network for use with Ceph.


Monitors
========

Ceph production clusters typically deploy with a minimum 3 :term:`Ceph Monitor`
daemons to ensure high availability should a monitor instance crash. At least
three (3) monitors ensures that the Paxos algorithm can determine which version
of the :term:`Ceph Cluster Map` is the most recent from a majority of Ceph
Monitors in the quorum.

.. note:: You may deploy Ceph with a single monitor, but if the instance fails,
	       the lack of other monitors may interrupt data service availability.

Ceph Monitors normally listen on port ``3300`` for the new v2 protocol, and ``6789`` for the old v1 protocol.

By default, Ceph expects that you will store a monitor's data under the
following path::

	/var/lib/ceph/mon/$cluster-$id

You or a deployment tool (e.g., ``ceph-deploy``) must create the corresponding
directory. With metavariables fully  expressed and a cluster named "ceph", the
foregoing directory would evaluate to::

	/var/lib/ceph/mon/ceph-a

For additional details, see the `Monitor Config Reference`_.

.. _Monitor Config Reference: ../mon-config-ref


.. _ceph-osd-config:


Authentication
==============

.. versionadded:: Bobtail 0.56

For Bobtail (v 0.56) and beyond, you should expressly enable or disable
authentication in the ``[global]`` section of your Ceph configuration file. ::

	auth cluster required = cephx
	auth service required = cephx
	auth client required = cephx

Additionally, you should enable message signing. See `Cephx Config Reference`_ for details.

.. _Cephx Config Reference: ../auth-config-ref


.. _ceph-monitor-config:


OSDs
====

Ceph production clusters typically deploy :term:`Ceph OSD Daemons` where one node
has one OSD daemon running a filestore on one storage drive. A typical
deployment specifies a journal size. For example:

.. code-block:: ini

	[osd]
	osd journal size = 10000

	[osd.0]
	host = {hostname} #manual deployments only.


By default, Ceph expects that you will store a Ceph OSD Daemon's data with the
following path::

	/var/lib/ceph/osd/$cluster-$id

You or a deployment tool (e.g., ``ceph-deploy``) must create the corresponding
directory. With metavariables fully  expressed and a cluster named "ceph", the
foregoing directory would evaluate to::

	/var/lib/ceph/osd/ceph-0

You may override this path using the ``osd data`` setting. We don't recommend
changing the default location. Create the default directory on your OSD host.

::

	ssh {osd-host}
	sudo mkdir /var/lib/ceph/osd/ceph-{osd-number}

The ``osd data`` path ideally leads to a mount point with a hard disk that is
separate from the hard disk storing and running the operating system and
daemons. If the OSD is for a disk other than the OS disk, prepare it for
use with Ceph, and mount it to the directory you just created::

	ssh {new-osd-host}
	sudo mkfs -t {fstype} /dev/{disk}
	sudo mount -o user_xattr /dev/{hdd} /var/lib/ceph/osd/ceph-{osd-number}

We recommend using the ``xfs`` file system when running
:command:`mkfs`.  (``btrfs`` and ``ext4`` are not recommended and no
longer tested.)

See the `OSD Config Reference`_ for additional configuration details.


Heartbeats
==========

During runtime operations, Ceph OSD Daemons check up on other Ceph OSD Daemons
and report their  findings to the Ceph Monitor. You do not have to provide any
settings. However, if you have network latency issues, you may wish to modify
the settings.

See `Configuring Monitor/OSD Interaction`_ for additional details.


.. _ceph-logging-and-debugging:

Logs / Debugging
================

Sometimes you may encounter issues with Ceph that require
modifying logging output and using Ceph's debugging. See `Debugging and
Logging`_ for details on log rotation.

.. _Debugging and Logging: ../../troubleshooting/log-and-debug


Example ceph.conf
=================

.. literalinclude:: demo-ceph.conf
   :language: ini

.. _ceph-runtime-config:



Running Multiple Clusters (DEPRECATED)
======================================

Some Ceph CLI commands take a ``-c`` (cluster name) option. This option is
present purely for backward compatibility. You should not attempt to deploy
or run multiple clusters on the same hardware, and it is recommended to always
leave the cluster name as the default ("ceph").

If you need to allow multiple clusters to exist on the same host, please use
:ref:`cephadm`, which uses containers to fully isolate each cluster.


.. _Hardware Recommendations: ../../../start/hardware-recommendations
.. _Network Configuration Reference: ../network-config-ref
.. _OSD Config Reference: ../osd-config-ref
.. _Configuring Monitor/OSD Interaction: ../mon-osd-interaction
