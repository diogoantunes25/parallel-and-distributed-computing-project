#! /bin/bash

function die() {
	cat <<EOF
Usage: benchmark version counts ns densities k

	- version: serial or omp or mpi
	- counts: comma separated list of values for generation count
	- ns: comma separated list of values for n
	- densities: comma separated list of values for density
	- k: different seed values per n/density pair



Runs benchmarks using specified version and configs.
EOF

	exit 1
}

function compile() {
	cd $version
	make life3d || (echo "Compilation failed" && exit 1)
	cd ..
}

if [[ $# -ne 5 ]]; then
	die
fi

if [[ $1 != serial ]] && [[ $1 != omp ]] && [[ $1 != mpi ]]; then
	echo "Invalid version. Must be serial, omp or mpi."
	die
fi

version=$1
counts=$2
ns=$3
densities=$4
k=$5

# Compile code

echo "Compiling life3d"
compile

oldIFS=$IFS

IFS=','

tmp_file="/tmp/asdfaerasdfasdf.tmp"
csv=../benchmarks/benchmark_$(date +%Y-%m-%d_%H-%M-%S).csv
echo "Saving results to $csv"
rm $csv || true
touch $csv
for count in $counts; do
	for n in $ns; do
		for density in $densities; do
			for ((seed = 1; seed <= k; seed++)); do
				echo "Running with gen_count=$count, n=$n, density=$density, seed=$seed"
				if [[ $1 == serial]]; then
					./$version/life3d $count $n $density $seed > /dev/null 2> $tmp_file
				else
					OMP_NUM_THREADS=6 ./$version/life3d-omp $count $n $density $seed > /dev/null 2> $tmp_file
				wall=$(tail -n 1 $tmp_file | grep -oE "[0-9]+(\.[0-9]+)?")
				echo "$count,$n,$density,$seed,$wall" >> $csv
			done
		done
	done
done

IFS=$oldIFS