
#include "python_bindings_common.h"

#include "export_headers.h"
#include "classad_wrapper.h"
#include "exception_utils.h"

BOOST_PYTHON_MODULE(classad)
{
	export_classad();

	auto scope = boost::python::scope();

	PyExc_ClassAdException = CreateExceptionInModule(
		"classad.ClassAdException", "ClassAdException",
		scope,
		PyExc_Exception,
		"Never raised.  The parent class of all exceptions raised by this module."
	);

	PyExc_ClassAdEnumError = CreateExceptionInModule(
		"classad.ClassAdEnumError", "ClassAdEnumError",
		scope,
		PyExc_ClassAdException, PyExc_TypeError,
		"Raised when a value must be in an enumeration, but isn't."
	);

	PyExc_ClassAdEvaluationError = CreateExceptionInModule(
		"classad.ClassAdEvaluationError", "ClassAdEvaluationError",
		scope,
		PyExc_ClassAdException, PyExc_TypeError, PyExc_RuntimeError,
		"Raised when the ClassAd library fails to evaluate an expression."
	);

	PyExc_ClassAdInternalError = CreateExceptionInModule(
		"classad.ClassAdInternalError", "ClassAdInternalError",
		scope,
		PyExc_ClassAdException, PyExc_ValueError, PyExc_RuntimeError,
		"Raised when the ClassAd library encounters an internal error."
	);

	PyExc_ClassAdParseError = CreateExceptionInModule(
		"classad.ClassAdParseError", "ClassAdParseError",
		scope,
		PyExc_ClassAdException, PyExc_ValueError, PyExc_SyntaxError, PyExc_RuntimeError,
		"Raised when the ClassAd library fails to parse a (putative) ClassAd."
	);

	PyExc_ClassAdValueError = CreateExceptionInModule(
		"classad.ClassAdValueError", "ClassAdValueError",
		scope,
		PyExc_ClassAdException, PyExc_ValueError, PyExc_RuntimeError, PyExc_TypeError,
		"Raised instead of :class:`ValueError` for backwards compatibility."
	);

	PyExc_ClassAdTypeError = CreateExceptionInModule(
		"classad.ClassAdTypeError", "ClassAdTypeError",
		scope,
		PyExc_ClassAdException, PyExc_TypeError, PyExc_ValueError,
		"Raised instead of :class:`TypeError` for backwards compatibility."
	);

	PyExc_ClassAdOSError = CreateExceptionInModule(
		"classad.ClassAdOSError", "ClassAdOSError",
		scope,
		PyExc_ClassAdException, PyExc_OSError, PyExc_RuntimeError,
		"Raised instead of :class:`OSError` for backwards compatibility."
	);
}
