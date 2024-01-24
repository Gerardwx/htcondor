#include "queue_connection.cpp"


static bool
_schedd_query_callback( void * r, ClassAd * ad ) {
    auto * results = static_cast<std::vector<ClassAd *> *>(r);
    results->push_back( ad );

    // Don't delete the pointers.  The `results` local below
    // will do that when it goes out of scope, after we've
    // copied everything into Python.
    return false;
}


static PyObject *
_schedd_query(PyObject *, PyObject * args) {
    // _schedd_query(addr, constraint, projection, limit, opts)

    const char * addr = NULL;
    const char * constraint = NULL;
    PyObject * projection = NULL;
    long limit = 0;
    long opts = 0;
    if(! PyArg_ParseTuple( args, "zzOll", & addr, & constraint, & projection, & limit, & opts )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    if(! PyList_Check(projection)) {
        PyErr_SetString(PyExc_TypeError, "projection must be a list");
        return NULL;
    }

    CondorQ q;
    // Required for backwards compatibility in version 1.
    q.requestServerTime(true);


    if( strlen(constraint) != 0 ) {
        q.addAND(constraint);
    }


    // FIXME: copied from _collector_query(), refactor.
    // FIXME: Commas aren't legal in attribute names.  Consider rewriting
    // to do the implosion on the Python side and a StringList here.
    std::vector<std::string> attributes;
    Py_ssize_t size = PyList_Size(projection);
    for( int i = 0; i < size; ++i ) {
        PyObject * py_attr = PyList_GetItem(projection, i);
        if( py_attr == NULL ) {
            // PyList_GetItem() has already set an exception for us.
            return NULL;
        }

        if(! PyUnicode_Check(py_attr)) {
            PyErr_SetString(PyExc_TypeError, "projection must be a list of strings");
            return NULL;
        }

        std::string attribute;
        if( py_str_to_std_string(py_attr, attribute) != -1 ) {
            attributes.push_back(attribute);
        } else {
            // py_str_to_std_str() has already set an exception for us.
            return NULL;
        }
    }

    // Why _don't_ we have a std::vector<std::string> constructor for these?
    StringList slAttributes;
    for( const auto & attribute : attributes ) {
        slAttributes.append(attribute.c_str());
    }


    CondorError errStack;
    ClassAd * summaryAd = NULL;
    std::vector<ClassAd *> results;
    int rv = q.fetchQueueFromHostAndProcess(
        addr, slAttributes, opts, limit,
        _schedd_query_callback, & results,
        2 /* use fetchQueueFromHostAndProcess2() */, & errStack,
        opts == CondorQ::fetch_SummaryOnly ? & summaryAd : NULL
    );

    switch( rv ) {
        case Q_OK:
            break;

        case Q_PARSE_ERROR:
        case Q_INVALID_CATEGORY:
            // This was ClassAdParseError in version 1.
            PyErr_SetString(PyExc_RuntimeError, "Parse error in constraint");
            return NULL;

        case Q_UNSUPPORTED_OPTION_ERROR:
            // This was HTCondorIOError in version 1.
            PyErr_SetString(PyExc_IOError, "Query fetch option unsupported by this schedd.");
            return NULL;

        default:
            // This was HTCondorIOError in version 1.
            std::string error = "Failed to fetch ads from schedd, errmsg="
                              + errStack.getFullText();
            PyErr_SetString(PyExc_IOError, error.c_str());
            return NULL;
    }


    PyObject * list = PyList_New(0);
    if( list == NULL ) {
        PyErr_SetString( PyExc_MemoryError, "_schedd_query" );
        return NULL;
    }

    if( opts == CondorQ::fetch_SummaryOnly ) {
        ASSERT(summaryAd != NULL);
        ASSERT(results.size() == 0);
        results.push_back(summaryAd);
    }

    for( auto & classAd : results ) {
        PyObject * pyClassAd = py_new_classad2_classad(classAd);
        auto rv = PyList_Append(list, pyClassAd);
        Py_DecRef(pyClassAd);

        if( rv != 0 ) {
            // PyList_Append() has already set an exception for us.
            return NULL;
        }
    }

    return list;
}


static PyObject *
_schedd_act_on_job_ids(PyObject *, PyObject * args) {
    // _schedd_act_on_job_ids(addr, job_list, action, reason_string, reason_code)

    const char * addr = NULL;
    const char * job_list = NULL;
    long action = 0;
    const char * reason_string = NULL;
    const char * reason_code = NULL;

    if(! PyArg_ParseTuple( args, "zzlzz", & addr, & job_list, & action, & reason_string, & reason_code )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    StringList ids(job_list);


    ClassAd * result = NULL;
    DCSchedd schedd(addr);
    switch( action ) {
        case JA_HOLD_JOBS:
            result = schedd.holdJobs(& ids, reason_string, reason_code, NULL, AR_TOTALS );
            break;

        case JA_RELEASE_JOBS:
            result = schedd.releaseJobs(& ids, reason_string, NULL, AR_TOTALS );
            break;

        case JA_REMOVE_JOBS:
            result = schedd.removeJobs(& ids, reason_string, NULL, AR_TOTALS );
            break;

        case JA_REMOVE_X_JOBS:
            result = schedd.removeXJobs(& ids, reason_string, NULL, AR_TOTALS );
            break;

        case JA_VACATE_JOBS:
        case JA_VACATE_FAST_JOBS:
            {
            auto vacate_type = action == JA_VACATE_JOBS ? VACATE_GRACEFUL : VACATE_FAST;
            result = schedd.vacateJobs(& ids, vacate_type, NULL, AR_TOTALS);
            } break;

        case JA_SUSPEND_JOBS:
            result = schedd.suspendJobs(& ids, reason_string, NULL, AR_TOTALS );
            break;

        case JA_CONTINUE_JOBS:
            result = schedd.continueJobs(& ids, reason_string, NULL, AR_TOTALS );
            break;

        default:
            // This was HTCondorEnumError, in version 1.
            PyErr_SetString(PyExc_RuntimeError, "Job action not implemented.");
            return NULL;
    }

    if( result == NULL ) {
        // This was HTCondorReplyError, in version 1.
        PyErr_SetString(PyExc_RuntimeError, "Error when performing action on the schedd.");
        return NULL;
    }


    return py_new_classad2_classad(result->Copy());
}


static PyObject *
_schedd_act_on_job_constraint(PyObject *, PyObject * args) {
    // _schedd_act_on_job_ids(addr, constraint, action, reason_string, reason_code)

    const char * addr = NULL;
    const char * constraint = NULL;
    long action = 0;
    const char * reason_string = NULL;
    const char * reason_code = NULL;

    if(! PyArg_ParseTuple( args, "zzlzz", & addr, & constraint, & action, & reason_string, & reason_code )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    if( constraint == NULL || constraint[0] == '\0' ) {
        constraint = "true";
    }


    ClassAd * result = NULL;
    DCSchedd schedd(addr);
    switch( action ) {
        case JA_HOLD_JOBS:
            result = schedd.holdJobs(constraint, reason_string, reason_code, NULL, AR_TOTALS );
            break;

        case JA_RELEASE_JOBS:
            result = schedd.releaseJobs(constraint, reason_string, NULL, AR_TOTALS );
            break;

        case JA_REMOVE_JOBS:
            result = schedd.removeJobs(constraint, reason_string, NULL, AR_TOTALS );
            break;

        case JA_REMOVE_X_JOBS:
            result = schedd.removeXJobs(constraint, reason_string, NULL, AR_TOTALS );
            break;

        case JA_VACATE_JOBS:
        case JA_VACATE_FAST_JOBS:
            {
            auto vacate_type = action == JA_VACATE_JOBS ? VACATE_GRACEFUL : VACATE_FAST;
            result = schedd.vacateJobs(constraint, vacate_type, NULL, AR_TOTALS);
            } break;

        case JA_SUSPEND_JOBS:
            result = schedd.suspendJobs(constraint, reason_string, NULL, AR_TOTALS );
            break;

        case JA_CONTINUE_JOBS:
            result = schedd.continueJobs(constraint, reason_string, NULL, AR_TOTALS );
            break;

        default:
            // This was HTCondorEnumError, in version 1.
            PyErr_SetString(PyExc_RuntimeError, "Job action not implemented.");
            return NULL;
    }

    if( result == NULL ) {
        // This was HTCondorReplyError, in version 1.
        PyErr_SetString(PyExc_RuntimeError, "Error when performing action on the schedd.");
        return NULL;
    }


    return py_new_classad2_classad(result->Copy());
}


static PyObject *
_schedd_edit_job_ids(PyObject *, PyObject * args) {
    // schedd_edit_job_ids(addr, job_list, attr, value, flags)

    const char * addr = NULL;
    const char * job_list = NULL;
    const char * attr = NULL;
    const char * value = NULL;
    long flags = 0;

    if(! PyArg_ParseTuple( args, "zzzzl", & addr, & job_list, & attr, & value, & flags )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    flags = flags | SetAttribute_NoAck;


    QueueConnection qc;
    if(! qc.connect(addr)) {
        // This was HTCondorIOError, in version 1.
        PyErr_SetString(PyExc_IOError, "Failed to connect to schedd.");
        return NULL;
    }

    StringList ids(job_list);


    long matchCount = 0;
    const char * id = NULL;
    for( ids.rewind(); (id = ids.next()) != NULL; ) {
        JOB_ID_KEY jobIDKey;
        if(! jobIDKey.set(id)) {
            qc.abort();

            // This was HTCondorValueError, in version 1.
            PyErr_SetString(PyExc_ValueError, "Invalid ID");
            return NULL;
        }

        int rv = SetAttribute(jobIDKey.cluster, jobIDKey.proc, attr, value, flags);
        if( rv == -1 ) {
            qc.abort();

            // This was HTCondorIOError, in version 1.
            PyErr_SetString(PyExc_RuntimeError, "Unable to edit job");
            return NULL;
        }

        matchCount += 1;
    }


    std::string message;
    if(! qc.commit(message)) {
        // This was HTCondorIOError, in version 1.
        PyErr_SetString(PyExc_RuntimeError, ("Unable to commit transaction:" + message).c_str());
        return NULL;
    }

    return PyLong_FromLong(matchCount);
}


static PyObject *
_schedd_edit_job_constraint(PyObject *, PyObject * args) {
    // schedd_edit_job_ids(addr, constraint, attr, value, flags)

    const char * addr = NULL;
    const char * constraint = NULL;
    const char * attr = NULL;
    const char * value = NULL;
    long flags = 0;

    if(! PyArg_ParseTuple( args, "zzzzl", & addr, & constraint, & attr, & value, & flags )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    if( constraint == NULL || constraint[0] == '\0' ) {
        constraint = "true";
    }

    if(! param_boolean("CONDOR_Q_ONLY_MY_JOBS", true)) {
        flags = flags | SetAttribute_OnlyMyJobs;
    }
    flags = flags | SetAttribute_NoAck;


    QueueConnection qc;
    if(! qc.connect(addr)) {
        // This was HTCondorIOError, in version 1.
        PyErr_SetString(PyExc_IOError, "Failed to connect to schedd.");
        return NULL;
    }

    int rval = SetAttributeByConstraint( constraint, attr, value, flags );
    if( rval == -1 ) {
        qc.abort();

        // This was HTCondorIOError, in version 1.
        PyErr_SetString(PyExc_IOError, "Unable to edit jobs matching constraint");
        return NULL;
    }


    std::string message;
    if(! qc.commit(message)) {
        // This was HTCondorIOError, in version 1.
        PyErr_SetString(PyExc_RuntimeError, ("Unable to commit transaction: " + message).c_str());
        return NULL;
    }

    return PyLong_FromLong(rval);
}


static PyObject *
_schedd_reschedule(PyObject *, PyObject * args) {
    // schedd_reschedule(addr)

    const char * addr = NULL;

    if(! PyArg_ParseTuple( args, "z", & addr )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }


    DCSchedd schedd(addr);
    Stream::stream_type st = schedd.hasUDPCommandPort() ? Stream::safe_sock : Stream::reli_sock;
    if(! schedd.sendCommand(RESCHEDULE, st, 0)) {
        dprintf( D_ALWAYS, "Can't send RESCHEDULE command to schedd.\n" );
    }

    Py_RETURN_NONE;
}


static PyObject *
_schedd_export_job_ids(PyObject *, PyObject * args) {
    // _schedd_export_job_ids(addr, job_list, export_dir, new_spool_dir)

    const char * addr = NULL;
    const char * job_list = NULL;
    const char * export_dir = NULL;
    const char * new_spool_dir = NULL;

    if(! PyArg_ParseTuple( args, "zzzz", & addr, & job_list, & export_dir, & new_spool_dir )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    StringList ids(job_list);


    CondorError errorStack;
    DCSchedd schedd(addr);
    ClassAd * result = schedd.exportJobs(
        & ids, export_dir, new_spool_dir, & errorStack
    );

    if( errorStack.code() > 0 ) {
        // This was HTCondorIOError in version 1.
        PyErr_SetString( PyExc_IOError, errorStack.getFullText(true).c_str() );
        return NULL;
    }
    if( result == NULL ) {
        // This was HTCondorIOError in version 1.
        PyErr_SetString( PyExc_IOError, "No result ad" );
        return NULL;
    }

    return py_new_classad2_classad(result->Copy());
}


static PyObject *
_schedd_export_job_constraint(PyObject *, PyObject * args) {
    // _schedd_export_job_constraint(addr, constraint, export_dir, new_spool_dir)

    const char * addr = NULL;
    const char * constraint = NULL;
    const char * export_dir = NULL;
    const char * new_spool_dir = NULL;

    if(! PyArg_ParseTuple( args, "zzzz", & addr, & constraint, & export_dir, & new_spool_dir )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    if( constraint == NULL || constraint[0] == '\0' ) {
        constraint = "true";
    }


    CondorError errorStack;
    DCSchedd schedd(addr);
    ClassAd * result = schedd.exportJobs(
        constraint, export_dir, new_spool_dir, & errorStack
    );

    if( errorStack.code() > 0 ) {
        // This was HTCondorIOError in version 1.
        PyErr_SetString( PyExc_IOError, errorStack.getFullText(true).c_str() );
        return NULL;
    }
    if( result == NULL ) {
        // This was HTCondorIOError in version 1.
        PyErr_SetString( PyExc_IOError, "No result ad" );
        return NULL;
    }


    return py_new_classad2_classad(result->Copy());
}


static PyObject *
_schedd_import_exported_job_results(PyObject *, PyObject * args) {
    // schedd_reschedule(addr, import_dir)

    const char * addr = NULL;
    const char * import_dir = NULL;

    if(! PyArg_ParseTuple( args, "zz", & addr, & import_dir )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }


    DCSchedd schedd(addr);
    CondorError errorStack;
    ClassAd * result = schedd.importExportedJobResults( import_dir, & errorStack );

    // Check for errorStack.code()?

    if( result == NULL ) {
        // This was HTCondorIOError in version 1.
        PyErr_SetString( PyExc_IOError, "No result ad" );
        return NULL;
    }

    // Check for `Error` attribute in result?


    return py_new_classad2_classad(result->Copy());
}


static PyObject *
_schedd_unexport_job_ids(PyObject *, PyObject * args) {
    // _schedd_unexport_job_constraint(addr, job_list)

    const char * addr = NULL;
    const char * job_list = NULL;

    if(! PyArg_ParseTuple( args, "zz", & addr, & job_list )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    StringList ids(job_list);


    DCSchedd schedd(addr);
    CondorError errorStack;
    ClassAd * result = schedd.unexportJobs( & ids, & errorStack );

    if( errorStack.code() > 0 ) {
        // This was HTCondorIOError in version 1.
        PyErr_SetString( PyExc_IOError, errorStack.getFullText(true).c_str() );
        return NULL;
    }
    if( result == NULL ) {
        // This was HTCondorIOError in version 1.
        PyErr_SetString( PyExc_IOError, "No result ad" );
        return NULL;
    }


    return py_new_classad2_classad(result->Copy());
}


static PyObject *
_schedd_unexport_job_constraint(PyObject *, PyObject * args) {
    // _schedd_unexport_job_constraint(addr, constraint)

    const char * addr = NULL;
    const char * constraint = NULL;

    if(! PyArg_ParseTuple( args, "zz", & addr, & constraint )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }

    if( constraint == NULL || constraint[0] == '\0' ) {
        constraint = "true";
    }


    DCSchedd schedd(addr);
    CondorError errorStack;
    ClassAd * result = schedd.unexportJobs( constraint, & errorStack );

    if( errorStack.code() > 0 ) {
        // This was HTCondorIOError in version 1.
        PyErr_SetString( PyExc_IOError, errorStack.getFullText(true).c_str() );
        return NULL;
    }
    if( result == NULL ) {
        // This was HTCondorIOError in version 1.
        PyErr_SetString( PyExc_IOError, "No result ad" );
        return NULL;
    }


    return py_new_classad2_classad(result->Copy());
}


int
submitProcAds( int clusterID, long count, SubmitBlob * sb, ClassAd * & clusterAd, int itemIndex = 0 ) {
    int numJobs = 0;

    //
    // Create new proc ads.
    //
    for( auto c = 0; c < count; ++c ) {
        int procID = NewProc( clusterID );
        if( procID < 0 ) {
            // This was HTCondorInternalError in version 1.
            PyErr_SetString( PyExc_RuntimeError, "Failed to create new proc ID." );
            return -1;
        }

        ClassAd * procAd = sb->make_job_ad( JOB_ID_KEY(clusterID, procID),
            itemIndex, c, false, false, NULL, NULL );
        // FIXME: do something with sb->error_stack().
        if(! procAd) {
            // This was HTCondorInternalError in version 1.
            PyErr_SetString( PyExc_RuntimeError, "Failed to create job ad" );
            return -1;
        }

        if( c == 0 ) {
            //
            // Send the cluster ad.
            //
            clusterAd = procAd->GetChainedParentAd();
            if(! clusterAd) {
                PyErr_SetString( PyExc_RuntimeError, "Failed to get parent ad" );
                return -1;
            }

            int rval = SendJobAttributes( JOB_ID_KEY(clusterID, -1),
                * clusterAd, SetAttribute_NoAck, sb->error_stack(), "Submit" );
            // FIXME: do something with sb->error_stack()
            if( rval < 0 ) {
                PyErr_SetString( PyExc_RuntimeError, "Failed to send cluster attributes" );
                return -1;
            }
        }

        int rval = SendJobAttributes( JOB_ID_KEY(clusterID, procID),
            * procAd, SetAttribute_NoAck, sb->error_stack(), "Submit" );
        if( rval < 0 ) {
            PyErr_SetString( PyExc_RuntimeError, "Failed to send proc attributes" );
            return -1;
        }
        ++numJobs;
    }

    return numJobs;
}


static PyObject *
_schedd_submit( PyObject *, PyObject * args ) {
    // _schedd_submit(addr, submit.handle_t, count, spool)

    const char * addr = NULL;
    PyObject_Handle * handle = NULL;
    long count = 0;
    int spool = 0;

    if(! PyArg_ParseTuple( args, "zOlp", & addr, (PyObject **)& handle, & count, & spool )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }


    SubmitBlob * sb = (SubmitBlob *)handle->t;


    QueueConnection qc;
    DCSchedd schedd(addr);
    if(! qc.connect(schedd)) {
        // This was HTCondorIOError, in version 1.
        PyErr_SetString(PyExc_IOError, "Failed to connect to schedd.");
        return NULL;
    }


    // Handle SUBMIT_SKIP_FILECHECKS.
    sb->setDisableFileChecks( param_boolean_crufty("SUBMIT_SKIP_FILECHECKS", true) ? 1 : 0 );

    // Set the remote schedd version.
    if( schedd.version() != NULL ) {
        sb->setScheddVersion( schedd.version() );
    } else {
        sb->setScheddVersion( CondorVersion() );
    }

    // Initialize the new cluster ad.
    if( sb->init_base_ad( time(NULL), schedd.getOwner().c_str() ) != 0 ) {
        qc.abort();

        std::string error = "Failed to create a cluster ad, errmsg="
            + sb->error_stack()->getFullText();

        // This was HTCondorInternalError in version 1.
        PyErr_SetString( PyExc_RuntimeError, error.c_str() );
        return NULL;
    }

    // This ends up being a pointer to a member of (a member of) `sb`.
    ClassAd * clusterAd = NULL;
    // Get a new cluster ID.
    int clusterID = NewCluster();
    if( clusterID < 0 ) {
        qc.abort();

        // This was HTCondorInternalError in version 1.
        PyErr_SetString( PyExc_RuntimeError, "Failed to create new cluster." );
        return NULL;
    }

    if( count == 0 ) {
        count = sb->queueStatementCount();
        if( count < 0 ) {
            qc.abort();

            // This was HTCondorValueError in version 1.
            PyErr_SetString( PyExc_ValueError, "invalid Queue statement" );
            return NULL;
        }
    }


    // I probably shouldn't have to do this by hand.
    sb->setTransferMap(getProtectedURLMap());


    // Handle itemdata.
    SubmitForeachArgs * itemdata = sb->init_vars( clusterID );
    if( itemdata == NULL ) {
        qc.abort();

        PyErr_SetString( PyExc_ValueError, "invalid Queue statement" );
        return NULL;
    }

    int itemCount = itemdata->items.number();

    int numJobs = 0;
    if( itemCount == 0 ) {
        numJobs = submitProcAds( clusterID, count, sb, clusterAd );
        if(numJobs < 0) {
            qc.abort();

            // submitProcAds() has already set an exception for us.
            delete itemdata;
            return NULL;
        }
    } else {
        int itemIndex = 0;
        char * item = NULL;
        itemdata->items.rewind();
        while( (item = itemdata->items.next()) ) {
            if( itemdata->slice.selected( itemIndex, itemCount ) ) {
                sb->set_vars( itemdata->vars, item, itemIndex );
                int nj = submitProcAds( clusterID, count, sb, clusterAd, itemIndex );
                if( nj < 0 ) {
                    qc.abort();

                    // submitProcAds() has already set an exception for us.
                    delete itemdata;
                    return NULL;
                }
                numJobs += nj;
            }
            ++itemIndex;
        }
    }

    // Since we can't remove keys from the submit hash, set their values
    // to NULL.  We normally set NULL values to the empty string, but
    // doing it this way means we can distinguish between keys in the
    // submit hash and keys in the submit object.
    sb->cleanup_vars( itemdata->vars );
    delete itemdata;


    std::string message;
    if(! qc.commit(message)) {
        // This was HTCondorIOError, in version 1.
        PyErr_SetString(PyExc_RuntimeError, ("Unable to commit transaction: " + message).c_str());
        return NULL;
    }
    if(! message.empty()) {
        // The Python warning infrastructure will by default suppress
        // the second and subsequent warnings, but that's the standard.
        PyErr_WarnEx( PyExc_UserWarning,
            ("Submit succeeded with warning: " + message).c_str(), 2
        );
    }


    Stream::stream_type st = schedd.hasUDPCommandPort() ? Stream::safe_sock : Stream::reli_sock;
    if(! schedd.sendCommand(RESCHEDULE, st, 0)) {
        dprintf( D_ALWAYS, "Can't send RESCHEDULE command to schedd.\n" );
    }


    PyObject * pyClusterAd = py_new_classad2_classad(clusterAd->Copy());
    return py_new_htcondor2_submit_result( clusterID, 0, numJobs, pyClusterAd );
}


static PyObject *
_history_query(PyObject *, PyObject * args) {
    // _history_query( addr, constraint, projection, match, since, history_record_source, command )

    const char * addr = NULL;
    const char * constraint = NULL;
    const char * projection = NULL;
    long match = 0;
    const char * since = NULL;
    long history_record_source = -1;
    long command = -1;
    if(! PyArg_ParseTuple( args, "zzzlzll", & addr, & constraint, & projection, & match, & since, & history_record_source, & command )) {
        // PyArg_ParseTuple() has already set an exception for us.
        return NULL;
    }


    ClassAd commandAd;

    if( strlen(constraint) != 0 ) {
        if(! commandAd.AssignExpr(ATTR_REQUIREMENTS, constraint)) {
            // This was ClassAdParseError in version 1.
            PyErr_SetString( PyExc_ValueError, "invalid constraint" );
            return NULL;
        }
    }

    commandAd.Assign(ATTR_NUM_MATCHES, match);

    StringList sl(projection);
    std::string prefix = "";
    std::string projectionList = "{";
    const char * attr = NULL;
    for( sl.rewind(); (attr = sl.next()) != NULL; ) {
        projectionList += prefix + "\"" + attr + "\"";
        prefix = ", ";
    }
    projectionList += "}";
    commandAd.AssignExpr(ATTR_PROJECTION, projectionList.c_str());

    if( since != NULL && since[0] != '\0' ) {
        commandAd.AssignExpr("Since", since);
    }

    switch( history_record_source ) {
        case 0: /* HRS_SCHEDD_JOB_HIST */
            break;
        case 1: /* HRS_STARTD_JOB_HIST */
            commandAd.InsertAttr("HistoryRecordSource" /* FIXME */, "STARTD");
            break;
        case 2: /* HRS_JOB_EPOCH */
            commandAd.InsertAttr("HistoryRecordSource" /* FIXME */, "JOB_EPOCH");
            break;
        default:
            // This was HTCondorValueError in version 1.
            PyErr_SetString( PyExc_RuntimeError, "unknown history record source" );
            return NULL;
    }


    daemon_t dt = DT_SCHEDD;
    if( command == GET_HISTORY ) {
        dt = DT_STARTD;
    }
    Daemon d(dt, addr);

    CondorError errorStack;
    Sock * sock = d.startCommand( command, Stream::reli_sock, 0, & errorStack );
    if( sock == NULL ) {
        const char * message = errorStack.message(0);
        if( message == NULL || message[0] == '\0' ) {
            message = "Unable to connect to schedd";
            if( dt == DT_STARTD ) { message = "Unable to connect to startd"; }

            // This was HTCondorIOError in version 1.
            PyErr_SetString( PyExc_IOError, message );
            return NULL;
        }
    }

    if(! putClassAd( sock, commandAd )) {
        // This was HTCondorIOError in version 1.
         PyErr_SetString( PyExc_IOError, "Unable to send history request classad" );
        return NULL;
    }

    if(! sock->end_of_message()) {
        // This was HTCondorIOError in version 1.
        PyErr_SetString( PyExc_IOError, "Unable to send end-of-message" );
        return NULL;
    }

    std::vector<ClassAd> results;

    long long owner = -1;
    while( true ) {
        ClassAd result;

        if(! getClassAd( sock, result )) {
            // This was HTCondorIOError in version 1.
            PyErr_SetString( PyExc_IOError, "Failed to receive history ad." );
            return NULL;
        }

        // Why is our code base so broken?  This unconditionally clears `owner.`
        if( result.EvaluateAttrInt(ATTR_OWNER, owner) && (owner == 0) ) {
            if(! sock->end_of_message()) {
                // This was HTCondorIOError in version 1.
                PyErr_SetString( PyExc_IOError, "Failed to receive end-of-message." );
                return NULL;
            }
            sock->close();

            long long errorCode = -1;
            std::string errorMessage;
            if( result.EvaluateAttrInt(ATTR_ERROR_CODE, errorCode) &&
              errorCode != 0 &&
              result.EvaluateAttrString(ATTR_ERROR_STRING, errorMessage) ) {
                // This was HTCondorIOError in version 1.
                PyErr_SetString( PyExc_IOError, errorMessage.c_str() );
                return NULL;
            }

            long long malformedAds = -1;
            if( result.EvaluateAttrInt("MalformedAds", malformedAds) &&
              malformedAds != 0 ) {
                // This was HTCondorIOError in version 1.
                PyErr_SetString( PyExc_IOError, "Remote side had parse errors in history file" );
                return NULL;
            }

            long long matches = -1;
            if( (! result.EvaluateAttrInt(ATTR_NUM_MATCHES, matches)) ||
              matches != (long long)results.size() ) {
                // This was (incorrectly) HTCondorValueError in version 1.
                PyErr_SetString( PyExc_IOError, "Incorrect number of ads received" );
                return NULL;
            }

            break;
        }

        results.push_back(result);
    }

    PyObject * list = PyList_New(0);
    if( list == NULL ) {
        PyErr_SetString( PyExc_MemoryError, "_history_query" );
        return NULL;
    }

    for( auto & classAd : results ) {
        // We could probably dispense with the copy by allocating the
        // result ads on the heap in the first place, but that would
        // require more attention to clean-up in the preceeding loop.
        PyObject * pyClassAd = py_new_classad2_classad(classAd.Copy());
        auto rv = PyList_Append(list, pyClassAd);
        Py_DecRef(pyClassAd);

        if( rv != 0 ) {
            // PyList_Append() has already set an exception for us.
            return NULL;
        }
    }

    return list;
}
