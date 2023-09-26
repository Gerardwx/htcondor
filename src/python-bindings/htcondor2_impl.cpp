#include "condor_common.h"

#define Py_LIMITED_API
#include <Python.h>

// We include some .cpp files, which is an awful hack, but I think less
// ugly, overall, than having or avoiding all of the defined-but-not-used
// warnings the compiler would otherwise generate.

// For module initialization.
#include "condor_config.h"
#include "common2/py_handle.cpp"

// htcondor.*
#include "condor_version.h"
#include "subsystem_info.h"
#include "htcondor2/loose_functions.cpp"

// htcondor.Collector
#include "daemon_list.h"
#include "common2/py_util.cpp"
#include "htcondor2/collector.cpp"

// htcondor.Negotiator
#include "htcondor2/negotiator.cpp"

// htcondor.Startd
#include "dc_startd.h"
#include "htcondor2/startd.cpp"

// htcondor.Credd
#include "store_cred.h"
#include "my_username.h"
#include "htcondor2/credd.cpp"

// htcondor.Schedd
#include "condor_q.h"
#include "dc_schedd.h"
#include "condor_qmgr.h"
#include "htcondor2/schedd.cpp"

// htcondor.Submit
#include "submit_utils.h"
#include "htcondor2/submit.cpp"


static PyMethodDef htcondor2_impl_methods[] = {
	{"_version", & _version, METH_VARARGS, R"C0ND0R(
        Returns the version of HTCondor this module is linked against.
    )C0ND0R"},

	{"_platform", & _platform, METH_VARARGS, R"C0ND0R(
        Returns the platform of HTCondor this module was compiled for.
    )C0ND0R"},

	{"_set_subsystem", & _set_subsystem, METH_VARARGS, R"C0ND0R(
        Set the subsystem name for the object.

        The subsystem is primarily used for the parsing of the HTCondor configuration file.

        :param str name: The subsystem name.
        :param daemon_type: The HTCondor daemon type. The default value of :attr:`SubsystemType.Auto` infers the type from the name parameter.
        :type daemon_type: :class:`SubsystemType`
    )C0ND0R"},


	{"_collector_init", &_collector_init, METH_VARARGS, NULL},

	{"_collector_query", &_collector_query, METH_VARARGS, NULL},

	{"_collector_locate_local", & _collector_locate_local, METH_VARARGS, NULL},

	{"_collector_advertise", & _collector_advertise, METH_VARARGS, NULL},


	{"_negotiator_command", &_negotiator_command, METH_VARARGS, NULL},

	{"_negotiator_command_return", &_negotiator_command_return, METH_VARARGS, NULL},

	{"_negotiator_command_user", &_negotiator_command_user, METH_VARARGS, NULL},

	{"_negotiator_command_user_return", &_negotiator_command_user_return, METH_VARARGS, NULL},

	{"_negotiator_command_user_value", &_negotiator_command_user_value, METH_VARARGS, NULL},


	{"_startd_drain_jobs", &_startd_drain_jobs, METH_VARARGS, NULL},

	{"_startd_cancel_drain_jobs", &_startd_cancel_drain_jobs, METH_VARARGS, NULL},


	{"_credd_do_store_cred", &_credd_do_store_cred, METH_VARARGS, NULL},

	{"_credd_do_check_oauth_creds", &_credd_do_check_oauth_creds, METH_VARARGS, NULL},


	{"_schedd_query", &_schedd_query, METH_VARARGS, NULL},

	{"_schedd_act_on_job_ids", &_schedd_act_on_job_ids, METH_VARARGS, NULL},

	{"_schedd_act_on_job_constraint", &_schedd_act_on_job_constraint, METH_VARARGS, NULL},

	{"_schedd_edit_job_ids", &_schedd_edit_job_ids, METH_VARARGS, NULL},

	{"_schedd_edit_job_constraint", &_schedd_edit_job_constraint, METH_VARARGS, NULL},

	{"_schedd_reschedule", &_schedd_reschedule, METH_VARARGS, NULL},

	{"_schedd_export_job_ids", &_schedd_export_job_ids, METH_VARARGS, NULL},

	{"_schedd_export_job_constraint", &_schedd_export_job_constraint, METH_VARARGS, NULL},

	{"_schedd_import_exported_job_results", &_schedd_import_exported_job_results, METH_VARARGS, NULL},

	{"_schedd_unexport_job_ids", &_schedd_unexport_job_ids, METH_VARARGS, NULL},

	{"_schedd_unexport_job_constraint", &_schedd_unexport_job_constraint, METH_VARARGS, NULL},


	{"_submit_init", &_submit_init, METH_VARARGS, NULL},

	{"_submit__getitem__", &_submit__getitem__, METH_VARARGS, NULL},

	{"_submit__setitem__", &_submit__setitem__, METH_VARARGS, NULL},

	{"_submit_keys", &_submit_keys, METH_VARARGS, NULL},


	{NULL, NULL, 0, NULL}
};


static struct PyModuleDef htcondor2_impl_module = {
	.m_base = PyModuleDef_HEAD_INIT,
	.m_name = "htcondor2_impl",
	.m_doc = NULL, /* no module documentation */
	.m_size = -1, /* this module has global state */
	.m_methods = htcondor2_impl_methods,

	// In C99, we could just leave these off.
	.m_slots = NULL,
	.m_traverse = NULL,
	.m_clear = NULL,
	.m_free = NULL,
};


PyMODINIT_FUNC
PyInit_htcondor2_impl(void) {
	// Initialization for HTCondor.  *sigh*
	config();

	// Control HTCondor's stderr verbosity with _CONDOR_TOOL_DEBUG.
	dprintf_set_tool_debug( "TOOL", 0 );

	PyObject * the_module = PyModule_Create(& htcondor2_impl_module);

	DynamicPyType_Handle dpt_handle("htcondor2_impl._handle");
	PyObject * pt_handle_object = PyType_FromSpec(& dpt_handle.type_spec);
	Py_INCREF(pt_handle_object);
	PyModule_AddObject(the_module, "_handle", pt_handle_object);

	return the_module;
}
