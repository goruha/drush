<?php

/**
 * Downloads the given package to the destination directory.
 *
 * @return
 *   The destination path on success, FALSE on failure.
 */
function make_download_factory($name, $download, $download_location) {
  $function = 'make_download_' . $download['type'];
  if (function_exists($function)) {
    return $function($name, $download, $download_location);
  }
  else {
    return FALSE;
  }
}

/**
 * Download project using drush's pm-download command.
 */
function make_download_pm($name, $download, $download_location) {
  $full_project_version = $name . '-' . $download['full_version'];

  $options = array(
    'destination' => dirname($download_location),
    'yes' => TRUE,
    'package-handler' => 'wget',
    'cache' => TRUE,
  );
  if ($name == 'drupal') {
    $options['drupal-project-rename'] = basename($download_location);
  }
  if (drush_get_option('no-cache', FALSE)) {
    unset($options['cache']);
  }

  $backend_options = array();
  if (!drush_get_option(array('verbose', 'debug'), FALSE)) {
    $backend_options['integrate'] = TRUE;
    $backend_options['log'] = FALSE;
  }

  // Perform actual download with `drush pm-download`.
  $return = drush_invoke_process('@none', 'pm-download', array($full_project_version), $options, $backend_options);
  if (empty($return['error_log'])) {
    drush_log(dt('@project downloaded from @url', array('@project' => $full_project_version, '@url' => $download['download_link'])), 'ok');
  }
}

/**
 * Downloads a file to the specified location.
 *
 * @return
 *   The destination directory on success, FALSE on failure.
 */
function make_download_file($name, $download, $download_location) {
  if ($filename = _make_download_file($download)) {
    if (!drush_get_option('ignore-checksums') && !_make_verify_checksums($download, $filename)) {
      return FALSE;
    }
    drush_log(dt('@project downloaded from @url.', array('@project' => $name, '@url' => $download['url'])), 'ok');
    return make_download_file_unpack($filename, $download_location, (isset($download['filename']) ? $download['filename'] : ''));
  }
  make_error('DOWNLOAD_ERROR', dt('Unable to download @project from @url.', array('@project' => $name, '@url' => $download['url'])));
  return FALSE;
}

function _make_download_file($download) {
  // TODO cleanup so $download is always a string, since with
  // drush_download_file(), that's all we pass in.
  if (is_string($download)) {
    $download = array('url' => $download);
  }

  // Default to use cache.
  $cache = drush_get_option('cache');
  if (!drush_get_option('no-cache', FALSE)) {
    drush_set_option('cache', TRUE);
  }
  drush_set_option('cache', $cache);

  $tmp_path = make_tmp();
  // Ensure that we aren't including the querystring when generating a filename
  // to save our download to.
  $file = basename(reset(explode('?', $download['url'], 2)));
  return drush_download_file($download['url'], $tmp_path . '/' . $file);
}

/**
 * Unpacks a file to the specified download location.
 *
 * @return
 *   The download location on success, FALSE on failure.
 */
function make_download_file_unpack($filename, $download_location, $name) {
  $extension = array_pop(explode('.', $filename));
  $success = FALSE;
  switch ($extension) {
    case 'gz':
    case 'tgz':
      // I'd like to just use tar -z, but apparently it breaks on windoze. Why do they always have to ruin everything?
      $success = make_download_file_unpack_gzip($filename, $download_location);
      break;
    case 'tar':
      $success = make_download_file_unpack_tar($filename, $download_location);
      break;
    case 'zip':
      $success = make_download_file_unpack_zip($filename, $download_location);
      break;
    default:
      // For files that don't require extraction, we must first make sure the
      // destination folder exists.
      if (drush_mkdir($download_location)) {
        // Now that the destination directory exists, we can now move the file.
        $destination = $download_location . ($name ? '/' . $name : '');
        $success = drush_shell_exec('mv %s %s', $filename, $destination);
      }
  }
  return $success ? $download_location : FALSE;
}

/**
 * Unpacks a tar file to the specified location.
 *
 * @return
 *   TRUE or FALSE depending on whether the operation was successful.
 */
function make_download_file_unpack_tar($filename, $download_location) {
  $tmp_path = make_tmp();

  list($main_directory) = array_reverse(explode('/', $download_location));

  drush_mkdir($tmp_path . '/__unzip__');
  drush_shell_exec('tar -x -C %s -f %s', $tmp_path . '/__unzip__', $filename);

  return _make_download_file_move($tmp_path, $filename, $download_location);
}

/**
 * Unpacks a gzip file to the specified location.
 *
 * @return
 *   TRUE or FALSE depending on whether the operation was successful.
 */
function make_download_file_unpack_gzip($filename, $download_location) {
  // Find out where contents will end up. Retrieve last column of output using awk.
  drush_shell_exec("gzip --list %s", $filename);
  $info = drush_shell_exec_output();
  if ($info) {
    foreach ($info as $line) {
      $matches = array();
      preg_match('/^\s+[0-9]+\s+[0-9-]+\s+[0-9\.%]+\s+(.*)$/', $line, $matches);
      if (isset($matches[1])) {
        $file = $matches[1];
        break;
      }
    }
    if (isset($file)) {
      // Unzip it and then delete the tar file.
      drush_shell_exec('gzip -d %s', $filename);
      return make_download_file_unpack_tar($file, $download_location);
    }
  }
}

function make_download_file_unpack_zip($filename, $download_location) {
  $tmp_path = make_tmp();

  list($main_directory) = array_reverse(explode('/', $download_location));

  drush_mkdir($tmp_path . '/__unzip__');
  drush_shell_exec("unzip %s -d %s", $filename, $tmp_path . '/__unzip__');

  return _make_download_file_move($tmp_path, $filename, $download_location);
}

function _make_download_file_move($tmp_path, $filename, $download_location) {
  drush_shell_exec('ls %s', $tmp_path . '/__unzip__');
  $lines = drush_shell_exec_output();
  $main_directory = basename($download_location);
  if (count($lines) == 1) {
    $directory = array_shift($lines);
    if ($directory != $main_directory) {
      drush_shell_exec('mv %s %s', $tmp_path . '/__unzip__/' . $directory, $tmp_path . '/__unzip__/' . $main_directory);
    }
    drush_shell_exec('cp -Rf %s %s', $tmp_path . '/__unzip__/' . $main_directory, dirname($download_location));
    drush_shell_exec('rm -rf %s', $tmp_path . '/__unzip__');
  }
  elseif (count($lines) > 1) {
    drush_shell_exec('rm -rf %s', $download_location);
    drush_shell_exec('mv %s %s', $tmp_path . '/__unzip__', $download_location);
  }

  // Remove the tarball.
  if (file_exists($filename)) {
    drush_shell_exec('rm %s', $filename);
  }

  if (file_exists($tmp_path . '/__unzip__')) {
    drush_shell_exec('rm -rf %s', $tmp_path . '/__unzip__');
  }
  return TRUE;
}


// Backwards compatibility.
function make_download_get($name, $download, $download_location) {
  return make_download_file($name, $download, $download_location);
}

/**
 * Checks out a git repository to the specified download location.
 *
 * Allowed parameters in $download, in order of precedence:
 *   - 'tag'
 *   - 'revision'
 *   - 'branch'
 *
 * This will also attempt to write out release information to the
 * .info file if the 'no-gitinfofile' option is FALSE. If
 * $download['full_version'] is present, this will be used, otherwise,
 * version will be set in this order of precedence:
 *   - 'tag'
 *   - 'branch'
 *   - 'revision'
 *
 * @return
 *   The download location on success, FALSE otherwise.
 */
function make_download_git($name, $download, $download_location) {
  $tmp_path = make_tmp();
  $wc = drush_get_option('working-copy');

  // If no download URL specified, assume anonymous clone from git.drupal.org.
  $download['url'] = isset($download['url']) ? $download['url'] : "git://git.drupal.org/project/$name.git";
  // If no working-copy download URL specified, assume it is the same.
  $download['wc_url'] = isset($download['wc_url']) ? $download['wc_url'] : $download['url'];

  // Use working-copy download URL if --working-copy specified.
  $url = $wc ? $download['wc_url'] : $download['url'];

  $tmp_location = drush_tempdir() . '/' . basename($download_location);

  // Before we can checkout anything, we need to clone the repository.
  if (!drush_shell_exec("git clone %s %s", $url, $tmp_location)) {
    make_error('DOWNLOAD_ERROR', dt('Unable to clone @project from @url.', array('@project' => $name, '@url' => $url)));
    return FALSE;
  }

  drush_log(dt('@project cloned from @url.', array('@project' => $name, '@url' => $url)), 'ok');

  // Get the current directory (so we can move back later).
  $cwd = getcwd();
  // Change into the working copy of the cloned repo.
  chdir($tmp_location);

  // Now that we've got a clone, we can checkout what the .make file says.
  // We want to use the most specific target possible, so first try a tag.
  if (!empty($download['tag'])) {
    // @TODO: change checkout to refs path
    if (drush_shell_exec("git checkout %s", 'refs/tags/' . $download['tag'])) {
      drush_log(dt("Checked out tag @tag.", array('@tag' => $download['tag'])), 'ok');
    }
    else {
      make_error('DOWNLOAD_ERROR', dt("Unable to check out tag @tag.", array('@tag' => $download['tag'])));
    }
  }

  // If there wasn't a tag, try a specific revision hash.
  elseif (!empty($download['revision'])) {
    if (drush_shell_exec("git checkout %s", $download['revision'])) {
      drush_log(dt("Checked out revision @revision.", array('@revision' => $download['revision'])), 'ok');
    }
    else {
      make_error('DOWNLOAD_ERROR', dt("Unable to checkout revision @revision", array('@revision' => $download['revision'])));
    }
  }

  // If not, see if we at least have a branch.
  elseif (!empty($download['branch'])) {
    if (drush_shell_exec("git checkout %s", $download['branch'])) {
      drush_log(dt("Checked out branch @branch.", array('@branch' => $download['branch'])), 'ok');
    }
    elseif (drush_shell_exec("git checkout -b %s %s", $download['branch'], 'origin/' . $download['branch'])) {
      drush_log(dt('Checked out branch origin/@branch.', array('@branch' => $download['branch'])), 'ok');
    }
    else {
      make_error('DOWNLOAD_ERROR', dt('Unable to check out branch @branch.', array('@branch' => $download['branch'])));
    }
  }

  if (!empty($download['submodule'])) {
    $command = 'git submodule update';
    foreach ($download['submodule'] as $option) {
      $command .= ' --%s';
    }
    if (call_user_func_array('drush_shell_exec', array_merge(array($command), $download['submodule']))) {
      drush_log(dt('Initialized registered submodules.'), 'ok');
    }
    else {
      make_error('DOWNLOAD_ERROR', dt('Unable to initialize submodules.'));
    }
  }

  // move back to last current directory (first line)
  chdir($cwd);

  // Handle .info file re-writing (if so desired).
  if (!drush_get_option('no-gitinfofile', FALSE)) {
    // Figure out the proper version string to use based on the .make file.
    // Best case is the .make file author told us directly.
    if (!empty($download['full_version'])) {
      $full_version = $download['full_version'];
    }
    // Next best is if we have a tag, since those are identical to versions.
    elseif (!empty($download['tag'])) {
      $full_version = $download['tag'];
    }
    // If we have a branch, append '-dev'.
    elseif (!empty($download['branch'])) {
      $full_version = $download['branch'] . '-dev';
    }
    // Ugh. Not sure what else we can do in this case.
    elseif (!empty($download['revision'])) {
      $full_version = $download['revision'];
    }
    // Probably can never reach this case.
    else {
      $full_version = 'unknown';
    }

    // If the version string ends in '.x-dev' do the Git magic to figure out
    // the appropriate 'rebuild version' string, e.g. '7.x-1.2+7-dev'.
    $matches = array();
    if (preg_match('/^(.+).x-dev$/', $full_version, $matches)) {
      require_once dirname(__FILE__) . '/../pm/package_handler/git_drupalorg.inc';
      $full_version = drush_pm_git_drupalorg_compute_rebuild_version($tmp_location, $matches[1]);
    }
    require_once dirname(__FILE__) . '/../pm/pm.drush.inc';
    drush_pm_inject_info_file_metadata($tmp_location, $name, $full_version);
  }

  // Remove .git/ directory if working-copy flag was not specified.
  if (!$wc && file_exists($tmp_location . '/.git')) {
    drush_shell_exec("rm -rf %s", $tmp_location . '/.git');
  }

  // Move the directory into the final resting location.
  drush_shell_exec('cp -Rf %s %s', $tmp_location, dirname($download_location));

  return dirname($tmp_location);
}

/**
 * Checks out a Bazaar repository to the specified download location.
 *
 * @return
 *   The download location on success, FALSE otherwise.
 */
function make_download_bzr($name, $download, $download_location) {
  $tmp_path = make_tmp();
  $tmp_location = drush_tempdir() . '/' . basename($download_location);
  if (!empty($download['url'])) {
    $args = array();
    $command = 'bzr';
    if (drush_get_option('working-copy')) {
      $command .= ' branch  --use-existing-dir';
    }
    else {
      $command .= ' export';
    }
    if (isset($download['revision'])) {
      $command .= ' -r %s';
      $args[] = $download['revision'];
    }
    $command .= ' %s %s';
    if (drush_get_option('working-copy')) {
      $args[] = $download['url'];
      $args[] = $tmp_location;
    }
    else {
      $args[] = $tmp_location;
      $args[] = $download['url'];
    }
    array_unshift($args, $command);
    if (call_user_func_array('drush_shell_exec', $args)) {
      drush_log(dt('@project downloaded from @url.', array('@project' => $name, '@url' => $download['url'])), 'ok');
      drush_shell_exec('cp -Rf %s %s', $tmp_location, dirname($download_location));
      return dirname($download_location);
    }
  }
  else {
    $download['url'] = dt("unspecified location");
  }
  make_error('DOWNLOAD_ERROR', dt('Unable to download @project from @url.', array('@project' => $name, '@url' => $download['url'])));
  drush_shell_exec('rm -rf %s', dirname($tmp_location));
  return FALSE;
}

/**
 * Checks out an SVN repository to the specified download location.
 *
 * @return
 *   The download location on success, FALSE otherwise.
 */
function make_download_svn($name, $download, $download_location) {
  if (!empty($download['url'])) {
    if (!empty($download['interactive'])) {
      $function = 'drush_shell_exec_interactive';
    }
    else {
      $options = ' --non-interactive';
      $function = 'drush_shell_exec';
    }
    if (!isset($download['force']) || $download['force']) {
      $options = ' --force';
    }
    if (drush_get_option('working-copy')) {
      $command = 'svn' . $options . ' checkout';
    }
    else {
      $command = 'svn' . $options . ' export';
    }

    $args = array();

    if (isset($download['revision'])) {
      $command .= ' -r%s';
      $args[] = $download['revision'];
    }

    $command .= ' %s %s';
    $args[] = $download['url'];
    $args[] = $download_location;

    if (!empty($download['username'])) {
      $command .= ' --username %s';
      $args[] = $download['username'];
      if (!empty($download['password'])) {
        $command .= ' --password %s';
        $args[] = $download['password'];
      }
    }
    array_unshift($args, $command);
    $result = call_user_func_array($function, $args);
    if ($result) {
      drush_log(dt('@project @command from @url.', array('@project' => $name, '@command' => $command, '@url' => $download['url'])), 'ok');
      return $download_location;
    }
    else {
      $download['url'] = dt("unspecified location");
    }
  }
  else {
    make_error('DOWNLOAD_ERROR', dt('Unable to download @project from @url.', array('@project' => $name, '@url' => $download['url'])));
    return FALSE;
  }
}

/**
 * Test that any supplied hash values match the hash of the file content.
 *
 * Unsupported hash algorithms are reported as failure.
 */
function _make_verify_checksums($info, $filename) {
  $hash_algos = array('md5', 'sha1', 'sha256', 'sha512');
  // We only have something to do if a key is an
  // available function.
  if (array_intersect(array_keys($info), $hash_algos)) {
    $content = file_get_contents($filename);
    foreach ($hash_algos as $algo) {
      if (!empty($info[$algo])) {
        $hash = _make_hash($algo, $content);
        if ($hash !== $info[$algo]) {
           make_error('DOWNLOAD_ERROR', dt('Checksum @algo verification failed for @file. Expected @expected, received @hash.', array('@algo' => $algo, '@file' => basename($filename), '@expected' => $info[$algo], '@hash' => $hash)));
          return FALSE;
        }
      }
    }
  }
  return TRUE;
}

/**
 * Calculate the hash of a string for a given algorithm.
 */
function _make_hash($algo, $string) {
  switch ($algo) {
    case 'md5':
      return md5($string);
    case 'sha1':
      return sha1($string);
    default:
      return function_exists('hash') ? hash($algo, $string) : '';
  }
}
