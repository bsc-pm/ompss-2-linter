#!/bin/bash
. "common.sh"

function print_usage() {
	echo "Usage: $me [OPTIONS] EXECUTABLE"
	echo "Options:   [-h] [-d <debug_level:{0..6}>] [-r <report_level:{'WRN','NTC'}>]"
	echo "           [-e] [-a <aggr_level:{0..1}>] [-p <pause_seconds:{0..}>]"
	echo "           [-o <outputs_directory>]"
	false
}

me=`basename "$0"`

debug_level=0
report_level="WRN"
extended_report=0
aggr_level=1
pause_seconds=0
outputs_directory="linter-data"

while getopts ":hd:r:ea:p:o:" opt; do
	case ${opt} in
		h )
			print_usage
			;;
		d )
			debug_level=$OPTARG

			if ! [[ $debug_level =~ ^[0-9]+$ ]]; then
				print_error "Invalid option: '-d' requires an integer in {0..6}"
			fi

			if [[ $debug_level -lt 0 ]] || [[ $debug_level -gt 6 ]]; then
				print_error "Invalid option: '-d' requires an integer in {0..6}"
			fi
			;;
		r )
			report_level=$OPTARG

			if [[ $report_level != "WRN" ]] && [[ $report_level != "NTC" ]]; then
				print_error "Invalid option: '-r' must be either 'WRN' or 'NTC'"
			fi
			;;
		e )
			extended_report=1
			;;
		a )
			aggr_level=$OPTARG

			if ! [[ $aggr_level =~ ^[0-9]+$ ]]; then
				print_error "Invalid option: '-a' requires an integer in {0..1}"
			fi

			if [[ $aggr_level -lt 0 ]] || [[ $aggr_level -gt 1 ]]; then
				print_error "Invalid option: '-a' requires an integer in {0..1}"
			fi
			;;
		p )
			pause_seconds=$OPTARG

			if ! [[ $pause_seconds =~ ^[0-9]+$ ]]; then
				print_error "Invalid option: '-p' requires a non-negative integer"
			fi

			if [[ $pause_seconds -lt 0 ]]; then
				print_error "Invalid option: '-p' requires a non-negative integer"
			fi
			;;
		o )
			outputs_directory=$OPTARG
			;;
		\? )
			print_usage
			;;
		: )
			print_error "Invalid option: $OPTARG requires an argument"
			;;
	esac
done
shift $((OPTIND -1))

executable=$@

if [[ $executable == "" ]]; then
	print_usage
fi

if [[ $pause_seconds == 0 ]]; then
	pin_tool="Linter.so"
	pause_tool_flag=""
else
	pin_tool="Linter-debug.so"
	pause_tool_flag="-pause_tool $pause_seconds"
fi

if [[ $extended_report == 0 ]]; then
	e_flag="-e"
else
	e_flag=""
fi

cmd="$PIN_ROOT/pin $pause_tool_flag -t obj-intel64/$pin_tool -d $debug_level -r $report_level $e_flag -a $aggr_level -o $outputs_directory -- $executable"

$cmd
