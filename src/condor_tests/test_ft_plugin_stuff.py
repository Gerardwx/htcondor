#!/usr/bin/env pytest

#testreq: personal
"""<<CONDOR_TESTREQ_CONFIG
	# make sure that file transfer plugins are enabled (might be disabled by default)
	ENABLE_URL_TRANSFERS = true
"""
#endtestreq

import logging

from ornithology import *
from pathlib import Path

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


@action
def tor_job(default_condor, path_to_sleep, test_dir):
    return default_condor.submit(
        {
            "executable":               path_to_sleep,
            "transfer_executable":      "True",
            "should_transfer_files":    "True",

            "log":                      test_dir / "tor_job.log",
            "hold":                     "True",

            "output":                   "output",
            "transfer_output_remaps":   '"output=badname://foo/bar"',
        },
        count=1,
    )


@action
def tor_job_requirements(tor_job):
    return str(tor_job.clusterad["Requirements"])


@action
def nph_job(default_condor, path_to_sleep, test_dir):
    name = Path(path_to_sleep).name
    handle = default_condor.submit(
        {
            "executable":               path_to_sleep,
            "transfer_executable":      "True",
            "should_transfer_files":    "True",
            "arguments":                1,

            "log":                      test_dir / "nph_job.log",

            "transfer_output_files":    name,
            "transfer_output_remaps":   f'"{name}=badname://foo/bar"',
            "requirements":             'TARGET.HasFileTransferPluginMethods =!= undefined',
        },
        count=1,
    )

    handle.wait(
        condition=ClusterState.all_held,
        fail_condition=ClusterState.any_complete,
        timeout=60,
    )

    return handle


class TestFTPluginStuff:

    def test_tor_requirements(self, tor_job_requirements):
        # How specific do we have to get here?
        assert 'badname' in tor_job_requirements

    def test_no_plugin_hold(self, nph_job):
        assert nph_job.state.all_held()
