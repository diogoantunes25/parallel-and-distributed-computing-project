#!/bin/bash

RED="\e[31m"
GREEN="\e[32m"
ENDCOLOR="\e[0m"

function die() {
    cat <<EOF
Usage: speedup.sh [n]

    - n: number of omp_THREADS to use in omp version

Runs both serial and omp versions and compares the speedup.
EOF

    exit 1
}

function compile() {
    cd serial
    echo "compile serial version"
    make clean && make|| (echo "compile failed" && exit 1)
    cd ../omp
    echo "compile omp version"
    make clean && make|| (echo "compile failed" && exit 1)
    cd ..
}

# Parse args

if [[ $# -gt 1 ]]; then
    die
fi

if [[ $# -eq 1 ]]; then
    n=$1
else
    die  
fi

# compile code

echo "compile life3d"
compile

# Run tests

total=0
failed=0

printf "\nRunning tests\n"
cd tests || exit 1
for file in $(ls -- *.in); do
    total=$((total+1))

    name="${file%.*}"
    in=$name.in
    out=$name.out
    err_serial=$name.err_serial
    err_omp=$name.err_omp
    myout_serial=$name.myout_serial
    myout_omp=$name.myout_omp
    diff_serial=$name.diff_serial
    diff_omp=$name.diff_omp

    printf "\n$name:\n"

    # Run with serial version
    ../serial/life3d $(cat $in) > $myout_serial 2> $err_serial
    diff $out $myout_serial > $diff_serial

    if [ -s $diff_serial ]; then
        echo -e "$RED serial failed$ENDCOLOR"
        failed=$((failed+1))
    else
        echo -e "$GREEN serial success$ENDCOLOR"
    fi

    # Calculate time
    time_serial=$(tail -n 1 $err_serial | grep -oE "[0-9]+(\.[0-9]+)?")
    echo "Time serial: $time_serial seconds"

    # Run with omp version
    OMP_NUM_THREADS=$n ../omp/life3d-omp $(cat $in) > $myout_omp 2> $err_omp
    diff $out $myout_omp > $diff_omp

    if [ -s $diff_omp ]; then
        echo -e "$RED omp failed$ENDCOLOR"
        failed=$((failed+1))
    else
        echo -e "$GREEN omp success$ENDCOLOR"
    fi

    # Calculate time and display speedup
    time_omp=$(tail -n 1 $err_omp | grep -oE "[0-9]+(\.[0-9]+)?")
    echo "Time omp: $time_omp seconds"

    if [[ $time_omp == 0.0 ]]; then
        echo "Cannot calculate speedup. Time omp is 0 seconds."
        continue
    else
	    python3 -c "print('Speedup: ' + str($time_serial/$time_omp))"
    fi
done

printf "\nFailed $failed/$total\n"

cd ..

if [[ $failed -gt 0 ]]; then
    exit 1
else
    exit 0
fi