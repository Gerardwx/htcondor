#include "condor_common.h"

#define Py_LIMITED_API
#include <Python.h>

// For module initialization.
#include "condor_config.h"
#include "htcondor2/py_handle.cpp"


// classad.*
#include "classad/classad.h"
#include "classad2/loose_functions.cpp"


// htcondor.*
#include "classad2/classad.cpp"


static PyMethodDef classad2_impl_methods[] = {
	{"_version", & _version, METH_VARARGS, R"C0ND0R(
        Returns the version of ClassAds this module is linked against.
    )C0ND0R"},

    {"_classad_init", & _classad_init, METH_VARARGS, NULL},
    {"_classad_to_string", & _classad_to_string, METH_VARARGS, NULL},
    {"_classad_get_item", & _classad_get_item, METH_VARARGS, NULL},

	{NULL, NULL, 0, NULL}
};


static struct PyModuleDef classad2_impl_module = {
	.m_base = PyModuleDef_HEAD_INIT,
	.m_name = "classad22_impl",
	.m_doc = NULL, /* no module documentation */
	.m_size = -1, /* this module has global state */
	.m_methods = classad2_impl_methods,

	// In C99, we could just leave these off.
	.m_slots = NULL,
	.m_traverse = NULL,
	.m_clear = NULL,
	.m_free = NULL,
};


PyMODINIT_FUNC
PyInit_classad2_impl(void) {
	// Initialization for HTCondor.  *sigh*
	config();

	// Control HTCondor's stderr verbosity with _CONDOR_TOOL_DEBUG.
	dprintf_set_tool_debug( "TOOL", 0 );

	PyObject * the_module = PyModule_Create(& classad2_impl_module);

	DynamicPyType_Handle dpt_handle("classad2_impl._handle");
	PyObject * pt_handle_object = PyType_FromSpec(& dpt_handle.type_spec);
	Py_INCREF(pt_handle_object);
	PyModule_AddObject(the_module, "_handle", pt_handle_object);

	return the_module;
}
