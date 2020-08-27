#!/usr/bin/env bash
set -ex

. $(dirname $0)/../../standalone/ceph-helpers.sh

POOL=rbd
NS=ns
IMAGE=testrbdnbd$$
SIZE=64
DATA=
DEV=

_sudo()
{
    local cmd

    if [ `id -u` -eq 0 ]
    then
	"$@"
	return $?
    fi

    # Look for the command in the user path. If it fails run it as is,
    # supposing it is in sudo path.
    cmd=`which $1 2>/dev/null` || cmd=$1
    shift
    sudo -nE "${cmd}" "$@"
}

setup()
{
    local ns x

    if [ -e CMakeCache.txt ]; then
	# running under cmake build dir

	CEPH_SRC=$(readlink -f $(dirname $0)/../../../src)
	CEPH_ROOT=${PWD}
	CEPH_BIN=${CEPH_ROOT}/bin

	export LD_LIBRARY_PATH=${CEPH_ROOT}/lib:${LD_LIBRARY_PATH}
	export PYTHONPATH=${PYTHONPATH}:${CEPH_SRC}/pybind:${CEPH_ROOT}/lib/cython_modules/lib.3
	PATH=${CEPH_BIN}:${PATH}
    fi

    _sudo echo test sudo

    trap cleanup INT TERM EXIT
    TEMPDIR=`mktemp -d`
    DATA=${TEMPDIR}/data
    dd if=/dev/urandom of=${DATA} bs=1M count=${SIZE}

    rbd namespace create ${POOL}/${NS}

    for ns in '' ${NS}; do
        rbd --dest-pool ${POOL} --dest-namespace "${ns}" --no-progress import \
            ${DATA} ${IMAGE}
    done
}

function cleanup()
{
    local ns s

    set +e

    mount | fgrep ${TEMPDIR}/mnt && umount ${TEMPDIR}/mnt

    rm -Rf ${TEMPDIR}
    if [ -n "${DEV}" ]
    then
	_sudo rbd-nbd unmap ${DEV}
    fi

    for ns in '' ${NS}; do
        if rbd -p ${POOL} --namespace "${ns}" status ${IMAGE} 2>/dev/null; then
	    for s in 0.5 1 2 4 8 16 32; do
	        sleep $s
	        rbd -p ${POOL} --namespace "${ns}" status ${IMAGE} |
                    grep 'Watchers: none' && break
	    done
	    rbd -p ${POOL} --namespace "${ns}" snap purge ${IMAGE}
	    rbd -p ${POOL} --namespace "${ns}" remove ${IMAGE}
        fi
    done
    rbd namespace remove ${POOL}/${NS}
}

function expect_false()
{
  if "$@"; then return 1; else return 0; fi
}

function get_pid()
{
    local ns=$1

    PID=$(rbd-nbd --format xml list-mapped | $XMLSTARLET sel -t -v \
      "//devices/device[pool='${POOL}'][namespace='${ns}'][image='${IMAGE}'][device='${DEV}']/id")
    test -n "${PID}"
    ps -p ${PID} -o cmd | grep rbd-nbd
}

unmap_device()
{
    local unmap_dev=$1
    local list_dev=${2:-$1}
    _sudo rbd-nbd unmap ${unmap_dev}

    for s in 0.5 1 2 4 8 16 32; do
	sleep ${s}
        rbd-nbd list-mapped | expect_false grep "${list_dev} *$" && return 0
    done
    return 1
}

#
# main
#

setup

# exit status test
expect_false rbd-nbd
expect_false rbd-nbd INVALIDCMD
if [ `id -u` -ne 0 ]
then
    expect_false rbd-nbd map ${IMAGE}
fi
expect_false _sudo rbd-nbd map INVALIDIMAGE
expect_false _sudo rbd-nbd --device INVALIDDEV map ${IMAGE}

# list format test
expect_false rbd-nbd --format INVALID list-mapped
rbd-nbd --format json --pretty-format list-mapped
rbd-nbd --format xml list-mapped

# map test using the first unused device
DEV=`_sudo rbd-nbd map ${POOL}/${IMAGE}`
get_pid
# map test specifying the device
expect_false _sudo rbd-nbd --device ${DEV} map ${POOL}/${IMAGE}
dev1=${DEV}
unmap_device ${DEV}
DEV=
# XXX: race possible when the device is reused by other process
DEV=`_sudo rbd-nbd --device ${dev1} map ${POOL}/${IMAGE}`
[ "${DEV}" = "${dev1}" ]
rbd-nbd list-mapped | grep "${IMAGE}"
get_pid

# read test
[ "`dd if=${DATA} bs=1M | md5sum`" = "`_sudo dd if=${DEV} bs=1M | md5sum`" ]

# write test
dd if=/dev/urandom of=${DATA} bs=1M count=${SIZE}
_sudo dd if=${DATA} of=${DEV} bs=1M oflag=direct
[ "`dd if=${DATA} bs=1M | md5sum`" = "`rbd -p ${POOL} --no-progress export ${IMAGE} - | md5sum`" ]

# trim test
provisioned=`rbd -p ${POOL} --format xml du ${IMAGE} |
  $XMLSTARLET sel -t -m "//stats/images/image/provisioned_size" -v .`
used=`rbd -p ${POOL} --format xml du ${IMAGE} |
  $XMLSTARLET sel -t -m "//stats/images/image/used_size" -v .`
[ "${used}" -eq "${provisioned}" ]
_sudo mkfs.ext4 -E discard ${DEV} # better idea?
sync
provisioned=`rbd -p ${POOL} --format xml du ${IMAGE} |
  $XMLSTARLET sel -t -m "//stats/images/image/provisioned_size" -v .`
used=`rbd -p ${POOL} --format xml du ${IMAGE} |
  $XMLSTARLET sel -t -m "//stats/images/image/used_size" -v .`
[ "${used}" -lt "${provisioned}" ]

# resize test
devname=$(basename ${DEV})
blocks=$(awk -v dev=${devname} '$4 == dev {print $3}' /proc/partitions)
test -n "${blocks}"
rbd resize ${POOL}/${IMAGE} --size $((SIZE * 2))M
rbd info ${POOL}/${IMAGE}
blocks2=$(awk -v dev=${devname} '$4 == dev {print $3}' /proc/partitions)
test -n "${blocks2}"
test ${blocks2} -eq $((blocks * 2))
rbd resize ${POOL}/${IMAGE} --allow-shrink --size ${SIZE}M
blocks2=$(awk -v dev=${devname} '$4 == dev {print $3}' /proc/partitions)
test -n "${blocks2}"
test ${blocks2} -eq ${blocks}

# read-only option test
unmap_device ${DEV}
DEV=`_sudo rbd-nbd map --read-only ${POOL}/${IMAGE}`
PID=$(rbd-nbd list-mapped | awk -v pool=${POOL} -v img=${IMAGE} -v dev=${DEV} \
    '$2 == pool && $3 == img && $5 == dev {print $1}')
test -n "${PID}"
ps -p ${PID} -o cmd | grep rbd-nbd

_sudo dd if=${DEV} of=/dev/null bs=1M
expect_false _sudo dd if=${DATA} of=${DEV} bs=1M oflag=direct
unmap_device ${DEV}

# exclusive option test
DEV=`_sudo rbd-nbd map --exclusive ${POOL}/${IMAGE}`
get_pid

_sudo dd if=${DATA} of=${DEV} bs=1M oflag=direct
expect_false timeout 10 \
	rbd bench ${IMAGE} --io-type write --io-size=1024 --io-total=1024
unmap_device ${DEV}
DEV=
rbd bench ${IMAGE} --io-type write --io-size=1024 --io-total=1024

# unmap by image name test
DEV=`_sudo rbd-nbd map ${POOL}/${IMAGE}`
get_pid
unmap_device ${IMAGE} ${DEV}
DEV=
ps -p ${PID} -o cmd | expect_false grep rbd-nbd

# map/unmap snap test
rbd snap create ${POOL}/${IMAGE}@snap
DEV=`_sudo rbd-nbd map ${POOL}/${IMAGE}@snap`
get_pid
unmap_device "${IMAGE}@snap" ${DEV}
DEV=
ps -p ${PID} -o cmd | expect_false grep rbd-nbd

# map/unmap namespace test
rbd snap create ${POOL}/${NS}/${IMAGE}@snap
DEV=`_sudo rbd-nbd map ${POOL}/${NS}/${IMAGE}@snap`
get_pid ${NS}
unmap_device "${POOL}/${NS}/${IMAGE}@snap" ${DEV}
DEV=
ps -p ${PID} -o cmd | expect_false grep rbd-nbd

# auto unmap test
DEV=`_sudo rbd-nbd map ${POOL}/${IMAGE}`
get_pid
_sudo kill ${PID}
for i in `seq 10`; do
  rbd-nbd list-mapped | expect_false grep "^${PID} *${POOL} *${IMAGE}" && break
  sleep 1
done
rbd-nbd list-mapped | expect_false grep "^${PID} *${POOL} *${IMAGE}"

# quiesce test
QUIESCE_HOOK=${TEMPDIR}/quiesce.sh
DEV=`_sudo rbd-nbd map --quiesce --quiesce-hook ${QUIESCE_HOOK} ${POOL}/${IMAGE}`

# test it does not fail if the hook does not exists
test ! -e ${QUIESCE_HOOK}
rbd snap create ${POOL}/${IMAGE}@quiesce1
_sudo dd if=${DATA} of=${DEV} bs=1M count=1 oflag=direct

# test the hook is executed
touch ${QUIESCE_HOOK}
chmod +x ${QUIESCE_HOOK}
cat > ${QUIESCE_HOOK} <<EOF
#/bin/sh
echo "test the hook is executed" >&2
echo \$1 > ${TEMPDIR}/\$2
EOF
rbd snap create ${POOL}/${IMAGE}@quiesce2
_sudo dd if=${DATA} of=${DEV} bs=1M count=1 oflag=direct
test "$(cat ${TEMPDIR}/quiesce)" = ${DEV}
test "$(cat ${TEMPDIR}/unquiesce)" = ${DEV}

# test it does not fail if the hook fails
touch ${QUIESCE_HOOK}
chmod +x ${QUIESCE_HOOK}
cat > ${QUIESCE_HOOK} <<EOF
#/bin/sh
echo "test it does not fail if the hook fails" >&2
exit 1
EOF
rbd snap create ${POOL}/${IMAGE}@quiesce3
_sudo dd if=${DATA} of=${DEV} bs=1M count=1 oflag=direct

# test the hook is slow
cat > ${QUIESCE_HOOK} <<EOF
#/bin/sh
echo "test the hook is slow" >&2
sleep 7
EOF
rbd snap create ${POOL}/${IMAGE}@quiesce4
_sudo dd if=${DATA} of=${DEV} bs=1M count=1 oflag=direct

# test rbd-nbd_quiesce hook that comes with distribution
unmap_device ${DEV}
LOG_FILE=${TEMPDIR}/rbd-nbd.log
if [ -n "${CEPH_SRC}" ]; then
    QUIESCE_HOOK=${CEPH_SRC}/tools/rbd_nbd/rbd-nbd_quiesce
    DEV=`_sudo rbd-nbd map --quiesce --quiesce-hook ${QUIESCE_HOOK} \
               ${POOL}/${IMAGE} --log-file=${LOG_FILE}`
else
    DEV=`_sudo rbd-nbd map --quiesce ${POOL}/${IMAGE} --log-file=${LOG_FILE}`
fi
_sudo mkfs ${DEV}
mkdir ${TEMPDIR}/mnt
_sudo mount ${DEV} ${TEMPDIR}/mnt
rbd snap create ${POOL}/${IMAGE}@quiesce5
_sudo dd if=${DATA} of=${TEMPDIR}/mnt/test bs=1M count=1 oflag=direct
_sudo umount ${TEMPDIR}/mnt
unmap_device ${DEV}
cat ${LOG_FILE}
expect_false grep 'quiesce failed' ${LOG_FILE}

echo OK
