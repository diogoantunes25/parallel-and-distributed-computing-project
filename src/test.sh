#! /bin/bash

RED="\e[31m"
GREEN="\e[32m"
ENDCOLOR="\e[0m"

function die() {
	cat <<EOF 
Usage: test [version]

    - version: serial or omp or mpi


Runs tests in test directory for specified version.
EOF

	exit 1
}

function compile() {
	cd $version
	make || (echo "Compilation failed" && exit 1)
	cd ..
}


# Parse args

if [[ $# -gt 1 ]]; then
	die
fi

if [[ $# -eq 0 ]]; then
	version=$(cat ../version);
else
	if [[ $1 != serial ]] && [[ $1 != omp ]] && [[ $1 != mpi ]]; then
		echo "Invalid version. Must be serial, omp or mpi."
		die
	fi

	version=$1
fi


# Compile code

echo "Compiling life3d"
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
	err=$name.err
	myout=$name.myout
	diff=$name.diff

	echo -n "$name:"
	if [[ $version == omp || $version == mpi ]]; then
		../$version/life3d-$version $(cat $in) > $myout 2> $err
	else
		../$version/life3d $(cat $in) > $myout 2> $err
	fi
	diff $out $myout > $diff

	if [ -s $diff ]; then
		echo -e "$RED failed$ENDCOLOR"
		failed=$((failed+1))
	else
		echo -e "$GREEN success$ENDCOLOR"
	fi
done

printf "\nFailed $failed/$total\n"

cd ..

if [[ $failed -gt 0 ]]; then
	exit 1
else
	exit 0
fi
