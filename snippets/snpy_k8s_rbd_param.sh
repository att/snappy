#!/bin/bash

kubectl_exec=/usr/bin/kubectl
jq_exec=/usr/bin/jq

# usage: get_pvc_name <pod_name> <vol_name> 
# return: pvc_name
# notice: the vol_name is the attached volume name in the pod, not pv name 

function get_pvc_name() {
	local pod_name=$1
	local vol_name=$2
	$kubectl_exec get po/$pod_name -o json | $jq_exec -r --arg vol_name "$vol_name" '.spec.volumes[] | select (.name==$vol_name) | .persistentVolumeClaim.claimName'

}

# usage: get_snpy_rbd_param <pvc_name>
# return: rbd_image, rbd_mon, rbd_pool, rbd_user and rbd_key

function get_snpy_rbd_param() {
	local pvc_name=$1
	local pv_name=`$kubectl_exec get pvc/$pvc_name -o json | $jq_exec -r .spec.volumeName`
	local pv_info=`$kubectl_exec get pv/$pv_name -o json`
	local rbd_image=`echo $pv_info | $jq_exec -r .spec.rbd.image`
 	local rbd_mon=`echo $pv_info | $jq_exec -r .spec.rbd.monitors`
 	local rbd_pool=`echo $pv_info | $jq_exec -r .spec.rbd.pool`
	local rbd_user=`echo $pv_info | $jq_exec -r .spec.rbd.user`
	local rbd_secret_name=`echo $pv_info | $jq_exec -r .spec.rbd.secretRef.name`
	local rbd_key=`$kubectl_exec get secrets/$rbd_secret_name -o json | $jq_exec -r .data.key | base64 --decode`

	echo $rbd_pool $rbd_image $rbd_mon $rbd_user $rbd_key
}
