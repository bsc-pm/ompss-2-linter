#!/bin/bash

set -e

function print_error() {
	echo "$1" 1>&2
	false
}

if [[ -z "$PIN_ROOT" ]]; then
	print_error "The PIN_ROOT envvar must be set in order to run this script."
fi
