<?php
/**
 * @file
 * Custom settings for test runs.
 */

// Never send telemetry data.
$settings['telemetry_enabled'] = FALSE;
// Turn off caching for tests.
$config['system.core']['cache'] = 0;
