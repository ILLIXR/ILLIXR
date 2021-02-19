#!/bin/bash

#--- Script defining bash helper functions ---#


# Function for printing a warning message (with red foreground)
function print_warning() { echo -e "\e[31m*Warning* ${1}\e[39m"; }
