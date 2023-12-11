#!/bin/bash -l
export PBS_JOBID=$SLURM_JOB_ID
export DARSHAN_LOGFILE=$DARSHAN_TMP/${PROG}.${PBS_JOBID}.darshan

mkdir /tmp/stress-tmp/
declare -a arr=("cpu","cpu-cache")
#"cpu-cache" "device" "io" "interrupt" "filesystem" "memory" "network" "os")

#for class_type in "${arr[@]}"
#do
#	{
	#taskset -c 0 stress-ng --temp-path /tmp/stress-tmp/ --class $class_type --tz -v --seq 0 | echo "$(date -d @$(date +%s.%N)): $class_type stressor started" &
	#taskset -c 0 stress-ng --temp-path /tmp/stress-tmp/ --class $class_type --tz -v --all 1 | echo "$(date -d @$(date +%s.%N)): $class_type stressor started" &
	#taskset -c 0 stress-ng --temp-path /tmp/stress-tmp/ --class $class_type --tz -v --all 2 | echo "$(date -d @$(date +%s.%N)): $class_type stressor started" &
	#taskset -c 0 stress-ng --temp-path /tmp/stress-tmp/ --class $class_type --tz -v --all 4 | echo "$(date -d @$(date +%s.%N)): $class_type stressor started" & 

	#taskset -c 0 stress-ng --temp-path /tmp/stress-tmp/ --class $class_type --tz -v --seq 0 -t 10s | echo "$(date -d @$(date +%s.%N)): $class_type stressor started" &
	#taskset -c 0 stress-ng --temp-path /tmp/stress-tmp/ --class $class_type --tz -v --all 1 -t 10s | echo "$(date -d @$(date +%s.%N)): $class_type stressor started" &
	#taskset -c 0 stress-ng --temp-path /tmp/stress-tmp/ --class $class_type --tz -v --all 2 -t 10s | echo "$(date -d @$(date +%s.%N)): $class_type stressor started" &
	#taskset -c 0 stress-ng --temp-path /tmp/stress-tmp/ --class $class_type --tz -v --all 4 -t 10s | echo "$(date -d @$(date +%s.%N)): $class_type stressor started" &

#	} 2> $DARSHAN_TMP/stress-ng.${PBS_JOBID}.err 
#done

START=$(date +%s.%N)
echo "$(date -d @$(date +%s.%N)): Application Started" 
srun --mpi=pmi2 $@ 
echo "$(date -d @$(date +%s.%N)): Application Ended"
END=$(date +%s.%N) 

#killall -2 stress-ng | echo "$(date -d @$(date +%s.%N)): Stressor Killed"

DIFF=$(echo "$END - $START" | bc)
echo "The DiffOfTime = $DIFF"

lfs getstripe /nscratch/spwalto/iorTest/darshan* > $DARSHAN_TMP/${PBS_JOBID}-${PROG}-OST.txt

# parse log with the dxt parser
$DARSHAN_PATH/bin/darshan-dxt-parser --show-incomplete $DARSHAN_LOGFILE > $DARSHAN_TMP/${PROG}.${PBS_JOBID}-dxt.darshan.txt
if [ $? -ne 0 ]; then
    echo "Error: failed to parse ${DARSHAN_LOGFILE} for dxt tracing" 1>&2
    exit 1
fi

# parse log with normal parser
$DARSHAN_PATH/bin/darshan-parser --all $DARSHAN_LOGFILE > $DARSHAN_TMP/${PROG}.${PBS_JOBID}.darshan.txt
if [ $? -ne 0 ]; then
    echo "Error: failed to parse ${DARSHAN_LOGFILE}" 1>&2
    exit 1
fi