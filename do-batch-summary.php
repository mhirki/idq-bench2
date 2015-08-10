#!/usr/bin/php
<?php

/*
 * Create a summary from multiple CSV files produced by micro benchmarks.
 */

$extract_columns = array('uops_issued_normal', 'idq_mite_normal', 'pkg_power_normal', 'uops_issued_extreme', 'idq_mite_extreme', 'pkg_power_extreme');

if ($argc > 1) {
	if ($argv[1] == "-h" || $argv[1] == "--help") {
		echo("Create a summary of CSV files produced by micro benchmarks.\n");
		echo("Usage: " . $argv[0] . " [ <column1> <column2> ... ]\n");
		exit(-1);
	}
	$extract_columns = array_slice($argv, 1);
}

function summarize_file($filename, $extract_columns) {
	$input_file = fopen($filename, "r");
	if ($input_file === FALSE) {
		echo("Error: Failed to open ${filename} for reading!\n");
		exit(-1);
	}
	
	// Read the first row from the CSV file which should contain the names of the columns
	while (($line = fgets($input_file)) !== FALSE) {
		if (strlen($line) > 0 && $line[0] != '#') {
			$column_names = trim($line);
			break;
		}
	}
	if (!isset($column_names)) {
		echo "Error: No column data found in input file!\n";
		exit(-1);
	}
	$columns = explode(",", $column_names);
	$num_columns = count($columns);
	if ($num_columns < 1) {
		echo "Error: Input data needs to have at least one column!\n";
		exit(-1);
	}
	$sscanf_pattern_array = array();
	for ($i = 0; $i < $num_columns; $i++) {
		$sscanf_pattern_array[] = "%f";
	}
	$sscanf_pattern = implode(",", $sscanf_pattern_array);
	
	// Find the columns we want to extract
	$extract_indices = array();
	$extracted_values = array();
	$extracted_medians = array();
	$num_extract_columns = count($extract_columns);
	foreach ($extract_columns as $col) {
		$index = array_search($col, $columns);
		if ($index === FALSE) {
			echo("Error: Column name ${col} not found in ${filename}!\n");
			exit(-1);
		}
		$extract_indices[] = $index;
		$extracted_values[] = array();
	}
	
	// Read the data
	while (($line = fgets($input_file)) !== FALSE) {
		if (strlen($line) > 0 && $line[0] != '#') {
			$values = sscanf($line, $sscanf_pattern);
			foreach ($extract_indices as $dest => $source) {
				$extracted_values[$dest][] = $values[$source];
			}
		}
	}
	
	// Sort the extracted values and find the median
	$num_rows = count($extracted_values[0]);
	$median_idx = floor($num_rows / 2);
	for ($i = 0; $i < $num_extract_columns; $i++) {
		sort($extracted_values[$i]);
		$extracted_medians[$i] = $extracted_values[$i][$median_idx];
	}
	
	return $extracted_medians;
}

function do_batch_summary($extract_columns) {
	$csv_files = glob("*.csv");
	
	// Print the output column names
	echo("name," . implode(",", $extract_columns) . "\n");
	
	foreach ($csv_files as $csv_file) {
		$row = summarize_file($csv_file, $extract_columns);
		echo(basename($csv_file, ".csv") . "," . implode(",", $row) . "\n");
	}
}

do_batch_summary($extract_columns);

?>
