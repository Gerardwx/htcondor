import os
import sys

from docutils import nodes
from docutils.parsers.rst import Directive
from sphinx import addnodes
from sphinx.errors import SphinxError
from sphinx.util.nodes import split_explicit_title, process_index_entry, set_role_source_info
from htc_helpers import custom_ext_parser, make_ref_and_index_nodes

def dump(obj):
    for attr in dir(obj):
        print("obj.%s = %r" % (attr, getattr(obj, attr)))

def macro_role(name, rawtext, text, lineno, inliner, options={}, content=[]):
    app = inliner.document.settings.env.app
    docname = inliner.document.settings.env.docname
    macro_name, macro_index = custom_ext_parser(text)
    ref_link = "href=\"../admin-manual/configuration-macros.html#" + str(macro_name) + "\""
    return make_ref_and_index_nodes(name, macro_name, macro_index,
                                    ref_link, rawtext, inliner, lineno, options)

def setup(app):
    app.add_role("macro", macro_role)

