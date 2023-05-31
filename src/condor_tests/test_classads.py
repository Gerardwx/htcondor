#!/usr/bin/env pytest

import pytest

import datetime

from htcondor2 import classad
# import classad

## Yes, I know, this is badly written, there's no test cases.

class TestClassAds:

    #
    # As of ddf0f0569a1825bc1d04ad8e90acd53bd5ed3e07, we need to test:
    #
    #  - classad.ClassAd()
    #  - classad.ClassAd(string)
    #  * classad.ClassAd(dictionary)
    #  * __repr__, __str__, and round-trips
    #  * __delitem__
    #  * __getitem__, __setitem__ for/with:
    #    * ClassAds, lists, recursion/intermixing
    #    - dictionaries
    #    - booleans, strings, integers, floats, byte strings
    #    - datetime.datetime (see below)
    #    - error, undefined [classad.Value]
    #    * ExprTrees / expressions
    #

    def test_equality_operators(self):
        c = classad.ClassAd()
        assert c == c
        assert not c != c

        d = classad.ClassAd()
        assert c == d
        assert c is not d


    def test_classad_string_constructor(self):
        c = classad.ClassAd("[]")
        assert c.get("foo") is None

        c = classad.ClassAd("[foo = 2 + 2]")
        assert type(c.get("foo")) is classad.ExprTree
        assert c["foo"] == c["foo"]

        c = classad.ClassAd("[foo = 2 + 2; bar = 2 + 2]")
        assert type(c.get("foo")) is classad.ExprTree
        assert type(c.get("bar")) is classad.ExprTree
        assert c.get("foo") is not c.get("bar")
        assert c["foo"] == c["bar"]

        c = classad.ClassAd("[foo = 2 + 2; bar = 2 + 3]")
        assert c["foo"] != c["bar"]

        d = classad.ClassAd()
        d["bar"] = c["bar"]
        assert d["bar"] == c["bar"]

        # Some day, we'll be more specific in classad2 with exceptions.
        with pytest.raises(RuntimeError):
            c = classad.ClassAd("")

        with pytest.raises(RuntimeError):
            c = classad.ClassAd("foo = 2 + i")

        with pytest.raises(RuntimeError):
            c = classad.ClassAd("[ 7, 8, 9 ]")


    def test_data_types(self):
        c = classad.ClassAd()

        c["a"] = True
        assert c["a"] is True

        c["b"] = False
        assert c["b"] is False

        c["c"] = -1.1
        assert c["c"] == -1.1

        c["v"] = classad.Value.Undefined
        assert c["v"] == classad.Value.Undefined

        if 'classad2' in str(type(c)):
            c["w"] = classad.Value.Error
            assert c["w"] == classad.Value.Error

        c["x"] = 7
        assert c["x"] == 7

        c["y"] = "eight"
        assert c["y"] == "eight"

        # In version 1, we converted byte strings to unicode strings,
        # and we do the same in version 2.
        c["z"] = b"seventy-five"
        assert c["z"] == "seventy-five"

        d = classad.ClassAd()
        c["nested_ad"] = d
        if 'classad2' in str(type(c)):
            # BUG: c["nested_ad"] is a dictionary in version 1.
            assert c["nested_ad"] == d
        assert c["nested_ad"] is not d

        d["a"] = "a"
        assert c["nested_ad"] != d
        assert c["nested_ad"] is not d

        e = classad.ClassAd()
        e["c"] = c
        if 'classad2' in str(type(c)):
            # BUG: e["c"] is a dictionary in version 1.
            assert e["c"] == c
        assert e["c"] is not c

        words = dict()
        words["a"] = "a"
        words["b"] = 7
        words["c"] = -1.1
        e["words"] = words
        if 'classad2' in str(type(c)):
            t = e["words"]
            for key in words.keys():
                assert t[key] == words[key]
        else:
            assert e["words"] == words

        # Testing classad.ExprTree round-trips would require a constructor,
        # so for now do the test after we test the ClassAd string constructor.


    # The v1 implementation of the ClassAd language's AbsTime type stores
    # all datetime.datetime objects as AbsTimes in UTC, which is wrong.
    # Instead, we adopt the Python convention that naive datetime.datetime
    # values are in the local timezone, and adjust them as appropriate.
    #
    # This is not bug-for-bug compatible, with version 1.
    #
    # For development convenience, this test detects the implementation and
    # makes the appropriate assertions.

    def test_ad_datetime(self):
        ad = classad.ClassAd()

        # Test an aware datetime
        then = datetime.datetime.now(tz=datetime.timezone.utc)
        ad["aware"] = then
        if 'classad2' in str(type(ad)):
            assert then.isoformat(sep=' ', timespec='seconds') == str(ad["aware"])
        else:
            assert then.isoformat(sep=' ', timespec='seconds') == f'{str(ad["aware"])}+00:00'

        # Test a naive datetime
        now = datetime.datetime.now()
        ad["naive"] = now

        if 'classad2' in str(type(ad)):
            # ClassAd time attributes are always stored in UTC, so convert
            # the naive datetime before comparing.
            offset = now.astimezone().utcoffset()
            now = now - offset

            assert f"{now.isoformat(sep=' ', timespec='seconds')}+00:00" == str(ad["naive"])
        else:
            assert f"{now.isoformat(sep=' ', timespec='seconds')}" == str(ad["naive"])

