DAGMan Workflows
================

DAGMan is a HTCondor tool that allows multiple jobs to be organized in
**workflows**, represented as a directed acyclic graph (DAG). A DAGMan workflow
automatically submits jobs in a particular order, such that certain jobs need
to complete before others start running. This allows the outputs of some jobs
to be used as inputs for others, and makes it easy to replicate a workflow
multiple times in the future.

.. sidebar:: Example DAG Visualized

    .. mermaid::
        :align: center

        flowchart TD
         A --> B


A simple example is a workflow that requires output from job ``A`` to become
input for job ``B``. This can be described as a DAGMan workflow as follows:

.. code-block:: condor-dagman

    # Example DAGMan Workflow
    JOB A produce_data.sub
    JOB B process_data.sub

    PARENT A CHILD B

.. note::

    A video introducing the DAGman tool for beginners is available at
    https://www.youtube.com/watch?v=1MvVHxRs7iU and another video, for
    intermediate users, is available at
    https://www.youtube.com/watch?v=C2RkdxE_ph0 .  A link to the slides is
    available in the videos' description.

.. toctree::
   :maxdepth: 2
   :glob:

   dagman-introduction
   dagman-interaction
   dagman-file-paths
   dagman-scripts
   node-pass-or-fail
   dagman-resubmit-failed
   dagman-save-files

   dagman-mulit-submit
   dagman-using-other-dags
   dagman-submit-optimization
   dagman-lots-of-jobs
   dagman-throttling

   dagman-node-types
   dagman-vars
   dagman-config
   dagman-scheduler-job-modification
   dagman-include
   dagman-all-nodes
   dagman-priorities
   dagman-accounting

   dagman-DOT-files
   dagman-node-status-file
   dagman-jobstate-log
   dagman-workflow-metrics
