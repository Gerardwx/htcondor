static PyObject_Handle *
get_handle_from(PyObject * py) {
	auto * py_handle = PyObject_GetAttrString( py, "_handle" );
	Py_DecRef(py_handle);
	return (PyObject_Handle *)py_handle;
}


static PyObject *
py_new_classad_classad(void * classAd) {
	static PyObject * py_htcondor_module = NULL;
	if( py_htcondor_module == NULL ) {
		 py_htcondor_module = PyImport_ImportModule( "htcondor" );
	}

	static PyObject * py_htcondor_classad_module = NULL;
	if( py_htcondor_classad_module == NULL ) {
		py_htcondor_classad_module = PyObject_GetAttrString( py_htcondor_module, "classad" );
	}

	static PyObject * py_ClassAd_class = NULL;
	if( py_ClassAd_class == NULL ) {
		py_ClassAd_class = PyObject_GetAttrString( py_htcondor_classad_module, "ClassAd" );
	}

	PyObject * pyClassAd = PyObject_CallObject(py_ClassAd_class, NULL);
	auto * handle = get_handle_from(pyClassAd);

	// PyObject_CallObject() should have called __init__() for us, which
	// should call _classad_init(), which should have made sure that the
	// Python ClassAd object is valid by creating a new ClassAd for it.
	if( handle->t != NULL ) { delete (ClassAd *)handle->t; }

	handle->t = (void *)classAd;
	// handle->f = [](void * & v) { dprintf( D_ALWAYS, "{classad::ClassAd}\n" ); delete (classad::ClassAd *)v; v = NULL; };
	return pyClassAd;
}


PyObject *
py_new_datetime_datetime(long secs) {
	static PyObject * py_datetime_module = NULL;
	if( py_datetime_module == NULL ) {
		 py_datetime_module = PyImport_ImportModule( "datetime" );
	}

	static PyObject * py_datetime_class = NULL;
	if( py_datetime_class == NULL ) {
		py_datetime_class = PyObject_GetAttrString( py_datetime_module, "datetime" );
	}

	return PyObject_CallMethodObjArgs(
	    py_datetime_class,
	    PyUnicode_FromString("fromtimestamp"),
	    PyLong_FromLong(secs)
	);
}


PyObject *
py_new_classad_value( classad::Value::ValueType vt ) {
	static PyObject * py_htcondor_module = NULL;
	if( py_htcondor_module == NULL ) {
		 py_htcondor_module = PyImport_ImportModule( "htcondor" );
	}

	static PyObject * py_htcondor_classad_module = NULL;
	if( py_htcondor_classad_module == NULL ) {
		py_htcondor_classad_module = PyObject_GetAttrString( py_htcondor_module, "classad" );
	}

	static PyObject * py_value_class = NULL;
	if( py_value_class == NULL ) {
		py_value_class = PyObject_GetAttrString( py_htcondor_classad_module, "Value" );
	}

	return PyObject_CallObject(py_value_class, PyLong_FromLong(vt));
}


//
// If we can't convert the ExprTree into a Python object and return that,
// make a deep copy of it, clear the parent scope, and return the
// ExprTree Python type with its _handle member set to the deep copy.
//
// If the API implies that the Python ExprTree can later be evaluated in
// the context of its (original) parent ad, then the Python ExprTree
// should have a member pointing to the `self` in this function.
//

PyObject *
py_new_classad_exprtree( ExprTree * original ) {
	static PyObject * py_htcondor_module = NULL;
	if( py_htcondor_module == NULL ) {
		 py_htcondor_module = PyImport_ImportModule( "htcondor" );
	}

	static PyObject * py_htcondor_classad_module = NULL;
	if( py_htcondor_classad_module == NULL ) {
		py_htcondor_classad_module = PyObject_GetAttrString( py_htcondor_module, "classad" );
	}

	static PyObject * py_exprtree_class = NULL;
	if( py_exprtree_class == NULL ) {
		py_exprtree_class = PyObject_GetAttrString( py_htcondor_classad_module, "ExprTree" );
	}

	ExprTree * copy = original->Copy();
	// ClassAd * parentScope = copy->GetParentScope();
	copy->SetParentScope(NULL);

	PyObject * pyExprTree = PyObject_CallObject(py_exprtree_class, NULL);
	auto * handle = get_handle_from(pyExprTree);

	// PyObject_CallObject() should have called __init__() for us, which
	// should call _exprtree_init(), which should have made sure that the
	// Python ClassAd object is valid by creating a new ExprTree for it.
	//
	// _exprtree_init() should have already set handle->f for us, as well.
	if( handle->t != NULL ) { delete (ClassAd *)handle->t; }

	handle->t = (void *)copy;
	return pyExprTree;
}


int
py_is_classad_exprtree(PyObject * py) {
	static PyObject * py_htcondor_module = NULL;
	if( py_htcondor_module == NULL ) {
		 py_htcondor_module = PyImport_ImportModule( "htcondor" );
	}

	static PyObject * py_htcondor_classad_module = NULL;
	if( py_htcondor_classad_module == NULL ) {
		py_htcondor_classad_module = PyObject_GetAttrString( py_htcondor_module, "classad" );
	}

	static PyObject * py_exprtree_class = NULL;
	if( py_exprtree_class == NULL ) {
		py_exprtree_class = PyObject_GetAttrString( py_htcondor_classad_module, "ExprTree" );
	}

	// The above should probably be factored out into its own function,
	// since it's identical in this one and in py_new_classad_exprtree.
	return PyObject_IsInstance( py, py_exprtree_class );
}


int
py_str_to_std_string(PyObject * py, std::string & str) {
	PyObject * py_bytes = PyUnicode_AsUTF8String(py);
	if( py_bytes == NULL ) {
		// PyUnicode_AsUTF8String() has already set an exception for us.
		return -1;
	}

	char * buffer = NULL;
	Py_ssize_t size = -1;
	if( PyBytes_AsStringAndSize(py_bytes, &buffer, &size) == -1 ) {
		// PyBytes_AsStringAndSize() has already set an exception for us.
		return -1;
	}

	str.assign( buffer, size );
	return 0;
}
