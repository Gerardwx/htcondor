import os
import sys

from docutils import nodes, utils
from docutils.parsers.rst import Directive
from sphinx import addnodes
from sphinx.errors import SphinxError
from sphinx.util.nodes import split_explicit_title, process_index_entry, set_role_source_info
from htc_helpers import custom_ext_parser

def dump(obj):
    for attr in dir(obj):
        print("obj.%s = %r" % (attr, getattr(obj, attr)))

def macro_def_role(name, rawtext, text, lineno, inliner, options={}, content=[]):
    app = inliner.document.settings.env.app
    docname = inliner.document.settings.env.docname

    # Create a new linkable target using the macro name
    knob, grouping = custom_ext_parser(text)
    targetid = knob
    targetnode = nodes.target('', knob, ids=[targetid], classes=["macro-def"])
    if grouping != "":
        grouping = grouping + " "
    # Automatically include an index entry for macro definitions
    indexnode = addnodes.index()
    indexnode['entries'] = process_index_entry(f"pair: {knob}; {grouping}Configuration Options", targetid)
    set_role_source_info(inliner, lineno, indexnode)

    return [indexnode, targetnode], []

def setup(app):
    app.add_role("macro-def", macro_def_role)

