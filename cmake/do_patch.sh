#!/usr/bin/env sh

print_help() {
  echo "This is a wrapper to the patch function which will return 0 if the patch has already been applied."
  echo "Unlike the patch command which will return a 1, indicating an error where there is not one. This"
  echo "script assumes that it is being run from the root directory of the patch file."
  echo "Usage:"
  echo "    do_patch.sh [options]"
  echo ""
  echo "Options:"
  echo "    -h   Display this help message and exit"
  echo ""
  echo "    -p   The name of the patch file to apply"
  echo ""
  exit $1
}

print_error() {
  echo ""
  echo ""
  print_help 1
}

while getopts h:p: flag
do
  case "${flag}" in
    h) print_help 0;;
    p) patchfile=${OPTARG};;
    *) print_error;;
  esac
done

if [ -z ${patchfile+x} ]
  then
    echo "No patch file given, use -p <patchfile>"
    exit 1;
fi

patch -p0 -N --input="$patchfile"
retCode=$?
if [ $retCode -gt 1 ]
 then
   exit $retCode
fi
exit 0
