ExprTree * convert_python_to_classad_exprtree(PyObject * py_v);

static PyObject *
_classad_init( PyObject *, PyObject * args ) {
    // _classad_init( self, self._handle )

    PyObject * self = NULL;
    PyObject_Handle * handle = NULL;
    if(! PyArg_ParseTuple( args, "OO", & self, (PyObject **)& handle )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    handle->t = new classad::ClassAd();
    handle->f = [](void * & v){ dprintf( D_ALWAYS, "[ClassAd]\n" ); delete (classad::ClassAd *)v; v = NULL; };
    Py_RETURN_NONE;
}


static PyObject *
_classad_init_from_string( PyObject *, PyObject * args ) {
    // _classad_init( self, self._handle, str )

    PyObject * self = NULL;
    PyObject_Handle * handle = NULL;
    const char * from_string = NULL;
    if(! PyArg_ParseTuple( args, "OOz", & self, (PyObject **)& handle, & from_string)) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    handle->f = [](void * & v){ dprintf( D_ALWAYS, "[unconstructed ClassAd]\n" ); if( v != NULL ) { dprintf(D_ALWAYS, "Error!  Unconstructed ClassAd has non-NULL handle %p\n", v); } };

    classad::ClassAdParser parser;
    classad::ClassAd * result = parser.ParseClassAd(from_string);
    if( result == NULL ) {
        // This was ClassAdParseError in version 1.
        PyErr_SetString( PyExc_SyntaxError, "Unable to parse string into a ClassAd." );
        return NULL;
    }

    handle->t = result;
    handle->f = [](void * & v){ dprintf( D_ALWAYS, "[ClassAd]\n" ); delete (classad::ClassAd *)v; v = NULL; };
    Py_RETURN_NONE;
}


static PyObject *
_classad_init_from_dict( PyObject *, PyObject * args ) {
    // _classad_init( self, self._handle, str )

    PyObject * self = NULL;
    PyObject_Handle * handle = NULL;
    PyObject * dict = NULL;
    if(! PyArg_ParseTuple( args, "OOO", & self, (PyObject **)& handle, & dict)) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    handle->t = convert_python_to_classad_exprtree(dict);
    handle->f = [](void * & v){ dprintf( D_ALWAYS, "[ClassAd]\n" ); delete (classad::ClassAd *)v; v = NULL; };
    Py_RETURN_NONE;
}


static PyObject *
_classad_to_string( PyObject *, PyObject * args ) {
    // _classad_to_string( self._handle )

    PyObject_Handle * handle = NULL;
    if(! PyArg_ParseTuple( args, "O", (PyObject **)& handle )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    std::string str;
    classad::PrettyPrint pp;
    pp.Unparse( str, (ClassAd *)handle->t );

    return PyUnicode_FromString(str.c_str());
}


static PyObject *
_classad_to_repr( PyObject *, PyObject * args ) {
    // _classad_to_string( self._handle )

    PyObject_Handle * handle = NULL;
    if(! PyArg_ParseTuple( args, "O", (PyObject **)& handle )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    std::string str;
    classad::ClassAdUnParser up;
    up.Unparse( str, (ClassAd *)handle->t );

    return PyUnicode_FromString(str.c_str());
}


bool
should_convert_to_python(classad::ExprTree * e) {
    auto kind = e->GetKind();

    if( kind == classad::ExprTree::EXPR_ENVELOPE ) {
        classad::CachedExprEnvelope * inner =
          static_cast<classad::CachedExprEnvelope*>(e);
        kind = inner->get()->GetKind();
    }

    switch( kind ) {
        case classad::ExprTree::LITERAL_NODE:
        case classad::ExprTree::CLASSAD_NODE:
        case classad::ExprTree::EXPR_LIST_NODE:
            return true;
        default:
            return false;
    }
}


PyObject *
convert_classad_value_to_python(classad::Value & v) {
    switch( v.GetType() ) {
        case classad::Value::CLASSAD_VALUE:
        case classad::Value::SCLASSAD_VALUE: {
            ClassAd * c;
            (void) v.IsClassAdValue(c);

            return py_new_htcondor2_classad(c->Copy());
            } break;

        case classad::Value::BOOLEAN_VALUE: {
            bool b;
            (void) v.IsBooleanValue(b);

            if(b) { Py_RETURN_TRUE; } else { Py_RETURN_FALSE; }
            } break;

        case classad::Value::STRING_VALUE: {
            std::string s;
            (void) v.IsStringValue(s);
            return PyUnicode_FromString(s.c_str());
            } break;

        case classad::Value::ABSOLUTE_TIME_VALUE: {
            classad::abstime_t atime; atime.secs=0; atime.offset=0;
            (void) v.IsAbsoluteTimeValue(atime);

            // An absolute time value is always in UTC.
            return py_new_datetime_datetime(atime.secs);
            } break;

        case classad::Value::RELATIVE_TIME_VALUE: {
            double d;
            (void) v.IsRelativeTimeValue(d);
            return PyFloat_FromDouble(d);
            } break;

        case classad::Value::INTEGER_VALUE: {
            long long ll;
            (void) v.IsIntegerValue(ll);
            return PyLong_FromLongLong(ll);
            } break;

        case classad::Value::REAL_VALUE: {
            double d;
            (void) v.IsRealValue(d);
            return PyFloat_FromDouble(d);
            } break;

        case classad::Value::ERROR_VALUE: {
            return py_new_classad_value(classad::Value::ERROR_VALUE);
            } break;

        case classad::Value::UNDEFINED_VALUE: {
            return py_new_classad_value(classad::Value::UNDEFINED_VALUE);
            } break;

        case classad::Value::LIST_VALUE:
        case classad::Value::SLIST_VALUE: {
            classad_shared_ptr<classad::ExprList> l;
            (void) v.IsSListValue(l);

            PyObject * list = PyList_New(0);
            if( list == NULL ) {
                PyErr_SetString( PyExc_MemoryError, "convert_classad_value_to_python" );
                return NULL;
            }

            for( auto i = l->begin(); i != l->end(); ++i ) {
                if( should_convert_to_python(*i) ) {
                    classad::Value v;
                    if(! (*i)->Evaluate( v )) {
                        Py_DecRef(list);
                        // This was ClassAdEvaluationError in version 1.
                        PyErr_SetString( PyExc_RuntimeError, "Failed to evaluate convertible expression" );
                        return NULL;
                    }

                    PyObject * py_v = convert_classad_value_to_python(v);
                    if(PyList_Append( list, py_v ) != 0) {
                        Py_DecRef(py_v);
                        Py_DecRef(list);
                        // PyList_Append() has already set an exception for us.
                        return NULL;
                    }
                    Py_DecRef(py_v);
                } else {
                    PyObject * py_v = py_new_classad_exprtree( *i );
                    if(PyList_Append( list, py_v ) != 0) {
                        Py_DecRef(py_v);
                        Py_DecRef(list);
                        // PyList_Append() has already set an exception for us.
                        return NULL;
                    }
                    Py_DecRef(py_v);
                }
            }

            return list;
            } break;

        default:
            // This was ClassAdEnumError in version 1.
            PyErr_SetString( PyExc_RuntimeError, "Unknown ClassAd value type" );
            return NULL;
    }
}


static PyObject *
_classad_get_item( PyObject *, PyObject * args ) {
    // _classad_get_item( self._handle, key, want_conversion )

    PyObject_Handle * handle = NULL;
    const char * key = NULL;
    int want_conversion = true;
    if(! PyArg_ParseTuple( args, "Osp", (PyObject **)& handle, & key, & want_conversion )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    auto * classAd = (ClassAd *)handle->t;
    classad::ExprTree * expr = classAd->Lookup( key );
    if(! expr) {
        PyErr_SetString( PyExc_KeyError, key );
        return NULL;
    }


    if( want_conversion && should_convert_to_python( expr ) ) {
        classad::Value v;
        if(! expr->Evaluate( v )) {
            // This was ClassAdEvaluationError in version 1.
            PyErr_SetString( PyExc_RuntimeError, "Failed to evaluate convertible expression" );
            return NULL;
        }

        return convert_classad_value_to_python(v);
    }


    return py_new_classad_exprtree( expr );
}


static PyObject *
_classad_del_item( PyObject *, PyObject * args ) {
    // _classad_del_item( self._handle, key )

    PyObject_Handle * handle = NULL;
    const char * key = NULL;
    if(! PyArg_ParseTuple( args, "Os", (PyObject **)& handle, & key )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    auto * classAd = (ClassAd *)handle->t;
    if(! classAd->Delete( key )) {
        // There's a bug in version 1 where del() never raises exceptions.
        PyErr_SetString( PyExc_KeyError, key );
        return NULL;
    }

    Py_RETURN_NONE;
}


ExprTree *
convert_python_to_classad_exprtree(PyObject * py_v) {
    if( py_v == Py_None ) {
        return classad::Literal::MakeUndefined();
    }

    int check = py_is_classad_exprtree(py_v);
    if( check == -1 ) { return NULL; }
    if( check == 1 ) {
        // `py_v` is a htcondor.ClassAd.ExprTree that owns its own ExprTree *,
        // so we have to return a copy here.  We don't have to deparent
        // the ExprTree, because it's already loose (by invariant).
        auto * handle = get_handle_from(py_v);
        return ((classad::ExprTree *)handle->t)->Copy();
    }

    check = py_is_htcondor2_classad(py_v);
    if( check == -1 ) { return NULL; }
    if( check == 1 ) {
        auto * handle = get_handle_from(py_v);
        return ((classad::ExprTree *)handle->t)->Copy();
    }

    classad::Value v;

    // We can't convert classad.Value to a native Python type in Python
    // because there's no native Python type for classad.Value.Error.
    check = py_is_classad_value(py_v);
    if( check == -1 ) { return NULL; }
    if( check == 1 ) {
        PyObject * py_v_int = PyNumber_Long(py_v);
        if( py_v_int == NULL ) {
            // This was ClassAdInternalError in version 1.
            PyErr_SetString( PyExc_RuntimeError, "Unknown ClassAd value type." );
            return NULL;
        }
        long lv = PyLong_AsLong(py_v_int);
        Py_DecRef(py_v_int);

        if( lv == 1<<0 ) {
            v.SetErrorValue();
            return classad::Literal::MakeLiteral(v);
        } else if( lv == 1<<1 ) {
            v.SetUndefinedValue();
            return classad::Literal::MakeLiteral(v);
        } else {
            // This was ClassAdInternalError in version 1.
            PyErr_SetString( PyExc_RuntimeError, "Unknown ClassAd value type." );
            return NULL;
        }
    }

    if( py_is_datetime_datetime(py_v) ) {
        // We do this song-and-dance in a bunch of places...
        static PyObject * py_htcondor2_module = NULL;
        if( py_htcondor2_module == NULL ) {
             py_htcondor2_module = PyImport_ImportModule( "htcondor2" );
        }

        static PyObject * py_htcondor2_classad_module = NULL;
        if( py_htcondor2_classad_module == NULL ) {
            py_htcondor2_classad_module = PyObject_GetAttrString( py_htcondor2_module, "classad" );
        }

        PyObject * py_ts = PyObject_CallMethod(
            py_htcondor2_classad_module,
            "_convert_local_datetime_to_utc_ts",
            "O", py_v
        );
        if( py_ts == NULL ) {
            // PyObject_CallMethod() has already set an exception for us.
            return NULL;
        }

        classad::abstime_t atime;
        atime.offset = 0;
        atime.secs = PyLong_AsLong(py_ts);
        if( PyErr_Occurred() ) {
            // PyLong_AsLong() has already set an exception for us.
            return NULL;
        }

        v.SetAbsoluteTimeValue(atime);
        return classad::Literal::MakeLiteral(v);
    }

    if( PyBool_Check(py_v) ) {
        v.SetBooleanValue( py_v == Py_True );
        return classad::Literal::MakeLiteral(v);
    }

    if( PyUnicode_Check(py_v) ) {
        PyObject * py_bytes = PyUnicode_AsUTF8String(py_v);

        char * buffer = NULL;
        Py_ssize_t size = -1;
        if( PyBytes_AsStringAndSize(py_bytes, &buffer, &size) == -1 ) {
            // PyBytes_AsStringAndSize() has already set an exception for us.
            return NULL;
        }
        v.SetStringValue( buffer, size );

        Py_DecRef(py_bytes);
        return classad::Literal::MakeLiteral(v);
    }

    if( PyBytes_Check(py_v) ) {
        char * buffer = NULL;
        Py_ssize_t size = -1;
        if( PyBytes_AsStringAndSize(py_v, &buffer, &size) == -1 ) {
            // PyBytes_AsStringAndSize() has already set an exception for us.
            return NULL;
        }
        v.SetStringValue( buffer, size );
        return classad::Literal::MakeLiteral(v);
    }

    if( PyLong_Check(py_v) ) {
        // This should almost certainly be ...AsLongLong(), but the
        // error is in the original version and in the classad3 module,
        // this function won't exist anyway.
        v.SetIntegerValue(PyLong_AsLong(py_v));
        return classad::Literal::MakeLiteral(v);
    }

    if( PyFloat_Check(py_v) ) {
        v.SetRealValue(PyFloat_AsDouble(py_v));
        return classad::Literal::MakeLiteral(v);
    }

    // PyDict_Check() is not part of the stable ABI.

    if( PyMapping_Check(py_v) ) {
        ClassAd * c = new classad::ClassAd();

        PyObject * items = PyMapping_Items(py_v);
        if( items == NULL ) {
            // A python list passes PyMapping_Check() because it supports
            // slices, but since it doesn't have keys, it fails here;
            // we'll catch in the last clause, below.  We do it in this
            // order because all dictionaries have iterators.
            PyErr_Clear();
        } else {
            Py_ssize_t size = PyList_Size(items);
            for( Py_ssize_t i = 0; i < size; ++i ) {
                PyObject * item = PyList_GetItem(items, i);

                const char * key = NULL;
                PyObject * value = NULL;
                if(! PyArg_ParseTuple( item, "zO", &key, &value)) {
                    // PyArg_ParseTuple() has already set an exception for us.
                    return NULL;
                }

                classad::ExprTree * e = convert_python_to_classad_exprtree(value);
                c->Insert(key, e);
            }
            Py_DecRef(items);

            return c;
        }
    }

    PyObject * iter = PyObject_GetIter(py_v);
    if( iter != NULL ) {
        classad::ExprList * exprList = new classad::ExprList();

        PyObject * item = NULL;
        while( (item = PyIter_Next(iter)) != NULL ) {
            exprList->push_back(convert_python_to_classad_exprtree(item));
            Py_DecRef(item);
        }
        Py_DecRef(iter);

        return exprList;
    } else {
        PyErr_Clear();
    }

    // This was ClassAdValueError in version 1.
    PyErr_SetString( PyExc_RuntimeError, "Unable to convert Python object to a ClassAd expression." );
    return NULL;
}


static PyObject *
_classad_set_item( PyObject *, PyObject * args ) {
    // _classad_set_item( self._handle, key, value )

    PyObject_Handle * handle = NULL;
    const char * key = NULL;
    PyObject * value = NULL;
    if(! PyArg_ParseTuple( args, "OsO", (PyObject **)& handle, & key, & value )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    auto * classAd = (ClassAd *)handle->t;
    ExprTree * v = convert_python_to_classad_exprtree(value);
    if(! classAd->Insert(key, v)) {
        if(! PyErr_Occurred()) {
            PyErr_SetString(PyExc_AttributeError, key);
        }
        return NULL;
    }

    Py_RETURN_NONE;
}


static PyObject *
_classad_size( PyObject *, PyObject * args ) {
    // _classad_size( self._handle )

    PyObject_Handle * handle = NULL;
    if(! PyArg_ParseTuple( args, "O", (PyObject **)& handle )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    auto * classAd = (ClassAd *)handle->t;
    long size = classAd->size();
    return PyLong_FromLong(size);
}


static PyObject *
_classad_keys( PyObject *, PyObject * args ) {
    // _classad_size( self._handle )

    PyObject_Handle * handle = NULL;
    if(! PyArg_ParseTuple( args, "O", (PyObject **)& handle )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    auto * classAd = (ClassAd *)handle->t;

    PyObject * list = PyList_New(0);
    if( list == NULL ) {
        PyErr_SetString( PyExc_MemoryError, "convert_classad_value_to_python" );
        return NULL;
    }

    for( auto i = classAd->begin(); i != classAd->end(); ++i ) {
        // dprintf( D_ALWAYS, "%s\n", i->first.c_str() );
        PyObject * py_str = PyUnicode_FromString(i->first.c_str());
        auto rv = PyList_Append( list, py_str );
        Py_DecRef(py_str);

        if(rv != 0) {
            // PyList_Append() has already set an exception for us.
            return NULL;
        }
    }

    return list;
}


bool
isOldAd( const char * in_string ) {
    while( in_string[0] != '\0' ) {
        // Don't know what the '/' is for, actually.
        if( in_string[0] == '/' || in_string[0] == '[' ) { return false; }
        if(! isspace(in_string[0])) { return true; }
        ++in_string;
    }
    return false;
}


static PyObject *
_classad_parse_next( PyObject *, PyObject * args ) {
    // _classad_parse_next(input_string, int(parser))

    long parser_type = -1;
    const char * from_string = NULL;
    if(! PyArg_ParseTuple( args, "zl", & from_string, & parser_type )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }


    // The 'auto' type from the ClassAdFileParseType and the 'auto' type
    // from version 1 both have weird, undocumented semantics, and they
    // disagree.  The 'auto' from version 1 only distinguished between
    // new and old ClassAd serializations, but supported a stream of new-form
    // ad serializations without requiring that they be a new ClassAd list,
    // must less one which had a newline after its opening left brace.
    //
    // Instead, we add a Python-only parse type, the default, which acts
    // like the version 1, and otherwise pass the other C++ types through.
    ClassAdFileParseType::ParseType pType = (ClassAdFileParseType::ParseType)parser_type;
    if( (long)pType == (long)-1 ) {
        pType = isOldAd(from_string) ? ClassAdFileParseType::Parse_long : ClassAdFileParseType::Parse_new;
    }

    size_t from_string_length = strlen(from_string);
    FILE * file = fmemopen( const_cast<char *>(from_string), from_string_length, "r" );
    if( file == NULL ) {
        // This was a ClassAdParseError in version 1.
        PyErr_SetString(PyExc_ValueError, "Unable to parse input stream into a ClassAd.");
        return NULL;
    }

    CondorClassAdFileIterator ccafi;
    if(! ccafi.begin( file, false, pType )) {
        // This was a ClassAdParseError in version 1.
        PyErr_SetString(PyExc_ValueError, "Unable to parse input stream into a ClassAd.");
        return NULL;
    }

    ClassAd * result = new ClassAd();
    int numAttrs = ccafi.next( * result );
    long offset = ftell( file );
    if( numAttrs <= 0 ) {
        if( offset == (long)from_string_length ) {
            Py_INCREF(Py_None);
            return Py_BuildValue("Oi", Py_None, 0);
        }

        // This was a ClassAdParseError in version 1.
        PyErr_SetString(PyExc_ValueError, "Unable to parse input stream into a ClassAd.");
        return NULL;
    }


    auto py_class_ad = py_new_htcondor2_classad(result);
    return Py_BuildValue("Ol", py_class_ad, offset);
}


static PyObject *
_classad_quote( PyObject *, PyObject * args ) {
    // _classad_quote(from_string)

    const char * from_string = NULL;
    if(! PyArg_ParseTuple( args, "z", & from_string )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }


    classad::Value v;
    v.SetStringValue(from_string);
    classad::ExprTree * expr = classad::Literal::MakeLiteral(v);
    classad::ClassAdUnParser sink;

    std::string result;
    sink.Unparse(result, expr);
    delete expr;


    return PyUnicode_FromString(result.c_str());
}
