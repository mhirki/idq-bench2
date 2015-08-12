#!/usr/bin/php
<?php

/*
 * Run all benchmarks in the current directory. Can be used for nightly runs.
 */

$initial_warmup_time = 240; // in seconds
$warmup_time = 120; // in seconds
$repeat_count = 100;
$benchmark_options = "-w ${warmup_time} -m -r ${repeat_count}";

$my_dir = dirname($argv[0]);
chdir($my_dir);

if ($argc > 1) {
	// Append arguments to benchmark options
	for ($i = 1; $i < $argc; $i++) {
		$benchmark_options .= " " . $argv[$i];
	}
}

function discover_benchmarks() {
	$files = glob("*");
	$benchmarks = array();
	
	foreach ($files as $file) {
		// All executable files except for .php scripts
		if (is_file($file) && is_executable($file) && substr($file, -4) !== ".php" && substr($file, -3) !== ".sh") {
			$benchmarks[] = $file;
		}
	}
	
	return $benchmarks;
}

function do_batch_run($benchmark_options, $initial_warmup_time) {
	$benchmarks = discover_benchmarks();
	
	$destination_dir = strftime("batch-runs-%Y-%m-%d_%H_%M_%S");
	mkdir($destination_dir);
	
	// Initial warmup
	if (count($benchmarks) > 0) {
		$benchmark = $benchmarks[0];
		shell_exec(escapeshellcmd("./${benchmark} -w ${initial_warmup_time}") . " > /dev/null");
	}
	
	foreach ($benchmarks as $benchmark) {
		shell_exec(escapeshellcmd("./${benchmark} ${benchmark_options}") . " > " . escapeshellarg("${destination_dir}/${benchmark}.csv"));
	}
}

do_batch_run($benchmark_options, $initial_warmup_time);

?>
