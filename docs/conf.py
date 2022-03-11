# -*- coding: utf-8 -*-
#
# Read the Docs Template documentation build configuration file, created by
# sphinx-quickstart on Tue Aug 26 14:19:49 2014.
#
# This file is execfile()d with the current directory set to its
# containing dir.
#
# Note that not all possible configuration values are present in this
# autogenerated file.
#
# All configuration values have a default; values that are commented out
# serve to show the default.

import sys
import os

import re

# If extensions (or modules to document with autodoc) are in another directory,
# add these directories to sys.path here. If the directory is relative to the
# documentation root, use os.path.abspath to make it absolute, like shown here.
# sys.path.insert(0, os.path.abspath('exts'))
sys.path.append(os.path.abspath('extensions'))

# -- General configuration ------------------------------------------------

# If your documentation needs a minimal Sphinx version, state it here.
# needs_sphinx = '1.0'

# Add any Sphinx extension module names here, as strings. They can be
# extensions coming with Sphinx (named 'sphinx.ext.*') or your custom
# ones.
extensions = [
    'sphinx.ext.autosectionlabel',
    'sphinx.ext.intersphinx',
    'sphinx.ext.autodoc',
    'sphinx.ext.napoleon',
    'ticket',
    'macro',
    'index',
    'jira',
]
autosectionlabel_prefix_document = True

# Add any paths that contain templates here, relative to this directory.
templates_path = ['_templates']

# The suffix of source filenames.
source_suffix = '.rst'

# The encoding of source files.
# source_encoding = 'utf-8-sig'

# The master toctree document.
master_doc = 'index'

# General information about the project.
project = u'HTCondor Manual'
copyright = u'1990-2020, Center for High Throughput Computing, Computer \
Sciences Department, University of Wisconsin-Madison, Madison, WI, US. \
Licensed under the Apache License, Version 2.0.'

# The version info for the project you're documenting, acts as replacement for
# |version| and |release|, also used in various other places throughout the
# built documents.
#
# The short X.Y version.
version = '8.8'
# The full version, including alpha/beta/rc tags.
release = '8.8.17'

rst_epilog = """
.. |release_date| replace:: Month Day, 2021
"""


# The language for content autogenerated by Sphinx. Refer to documentation
# for a list of supported languages.
# language = None

# There are two options for replacing |today|: either, you set today to some
# non-false value, then it is used:
# today = ''
# Else, today_fmt is used as the format for a strftime call.
# today_fmt = '%B %d, %Y'

# List of patterns, relative to source directory, that match files and
# directories to ignore when looking for source files.
exclude_patterns = ['_build', 'extensions', 'utils']

# The reST default role (used for this markup: `text`) to use for all
# documents.
# default_role = None

# If true, '()' will be appended to :func: etc. cross-reference text.
# add_function_parentheses = True

# If true, the current module name will be prepended to all description
# unit titles (such as .. function::).
# add_module_names = True

# If true, sectionauthor and moduleauthor directives will be shown in the
# output. They are ignored by default.
# show_authors = False

# The name of the Pygments (syntax highlighting) style to use.
pygments_style = 'sphinx'

# A list of ignored prefixes for module index sorting.
# modindex_common_prefix = []

# If true, keep warnings as "system message" paragraphs in the built documents.
# keep_warnings = False


# -- Options for HTML output ----------------------------------------------

# The theme to use for HTML and HTML Help pages.  See the documentation for
# a list of builtin themes.
html_theme = 'sphinx_rtd_theme'

# Theme options are theme-specific and customize the look and feel of a theme
# further.  For a list of options available for each theme, see the
# documentation.
# html_theme_options = {}

# Add any paths that contain custom themes here, relative to this directory.
# html_theme_path = []

# The name for this set of Sphinx documents.  If None, it defaults to
# "<project> v<release> documentation".
# html_title = None

# A shorter title for the navigation bar.  Default is the same as html_title.
# html_short_title = None

# The name of an image file (relative to this directory) to place at the top
# of the sidebar.
# html_logo = None

# The name of an image file (within the static path) to use as favicon of the
# docs.  This file should be a Windows icon file (.ico) being 16x16 or 32x32
# pixels large.
# html_favicon = None

# Add any paths that contain custom static files (such as style sheets) here,
# relative to this directory. They are copied after the builtin static files,
# so a file named "default.css" will overwrite the builtin "default.css".
html_static_path = ['_static']

# Add any extra paths that contain custom files (such as robots.txt or
# .htaccess) here, relative to this directory. These files are copied
# directly to the root of the documentation.
# html_extra_path = []

# If not '', a 'Last updated on:' timestamp is inserted at every page bottom,
# using the given strftime format.
# html_last_updated_fmt = '%b %d, %Y'

# If true, SmartyPants will be used to convert quotes and dashes to
# typographically correct entities.
# html_use_smartypants = True

# Custom sidebar templates, maps document names to template names.
# html_sidebars = {}

# Additional templates that should be rendered to pages, maps page names to
# template names.
# html_additional_pages = {}

# If false, no module index is generated.
# html_domain_indices = True

# If false, no index is generated.
# html_use_index = True

# If true, the index is split into individual pages for each letter.
# html_split_index = False

# If true, links to the reST sources are added to the pages.
# html_show_sourcelink = True

# If true, "Created using Sphinx" is shown in the HTML footer. Default is True.
# html_show_sphinx = True

# If true, "(C) Copyright ..." is shown in the HTML footer. Default is True.
# html_show_copyright = True

# If true, an OpenSearch description file will be output, and all pages will
# contain a <link> tag referring to it.  The value of this option must be the
# base URL from which the finished HTML is served.
# html_use_opensearch = ''

# This is the file name suffix for HTML files (e.g. ".xhtml").
# html_file_suffix = None

# Output file base name for HTML help builder.
htmlhelp_basename = 'ReadtheDocsTemplatedoc'

# -- Options for LaTeX output ---------------------------------------------

latex_elements = {
    # The paper size ('letterpaper' or 'a4paper').
    # 'papersize': 'letterpaper',

    # The font size ('10pt', '11pt' or '12pt').
    # 'pointsize': '10pt',

    # Additional stuff for the LaTeX preamble.
    # 'preamble': '',
}

# Grouping the document tree into LaTeX files. List of tuples
# (source start file, target name, title,
#  author, documentclass [howto, manual, or own class]).
latex_documents = [
    ('index', 'HTCondorManual.tex', u'HTCondor Manual',
     u'HTCondor Team', 'manual'),
]

# The name of an image file (relative to this directory) to place at the top of
# the title page.
# latex_logo = None

# For "manual" documents, if this is true, then toplevel headings are parts,
# not chapters.
# latex_use_parts = False

# If true, show page references after internal links.
# latex_show_pagerefs = False

# If true, show URL addresses after external links.
# latex_show_urls = False

# Documents to append as an appendix to all manuals.
# latex_appendices = []

# If false, no module index is generated.
# latex_domain_indices = True


# -- Options for manual page output ---------------------------------------

# One entry per manual page. List of tuples
# (source start file, name, description, authors, manual section).
man_pages = [
    ('man-pages/bosco_cluster', 'bosco_cluster', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/bosco_findplatform', 'bosco_findplatform', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/bosco_install', 'bosco_install', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/bosco_ssh_start', 'bosco_ssh_start', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/bosco_start', 'bosco_start', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/bosco_stop', 'bosco_stop', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/bosco_uninstall', 'bosco_uninstall', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_advertise', 'condor_advertise', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_annex', 'condor_annex', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_checkpoint', 'condor_checkpoint', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_check_userlogs', 'condor_check_userlogs', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_chirp', 'condor_chirp', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_cod', 'condor_cod', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_compile', 'condor_compile', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_configure', 'condor_configure', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_config_val', 'condor_config_val', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_continue', 'condor_continue', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_convert_history', 'condor_convert_history', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_dagman_metrics_reporter', 'condor_dagman_metrics_reporter', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_dagman', 'condor_dagman', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_drain', 'condor_drain', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_fetchlog', 'condor_fetchlog', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_findhost', 'condor_findhost', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_gather_info', 'condor_gather_info', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_gpu_discovery', 'condor_gpu_discovery', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_history', 'condor_history', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_hold', 'condor_hold', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_install', 'condor_install', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_job_router_info', 'condor_job_router_info', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_master', 'condor_master', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_now', 'condor_now', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_off', 'condor_off', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_on', 'condor_on', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_ping', 'condor_ping', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_pool_job_report', 'condor_pool_job_report', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_power', 'condor_power', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_preen', 'condor_preen', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_prio', 'condor_prio', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_procd', 'condor_procd', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_qedit', 'condor_qedit', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_q', 'condor_q', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_qsub', 'condor_qsub', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_reconfig', 'condor_reconfig', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_release', 'condor_release', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_reschedule', 'condor_reschedule', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_restart', 'condor_restart', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_rmdir', 'condor_rmdir', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_rm', 'condor_rm', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_router_history', 'condor_router_history', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_router_q', 'condor_router_q', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_router_rm', 'condor_router_rm', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_run', 'condor_run', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_set_shutdown', 'condor_set_shutdown', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_sos', 'condor_sos', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_ssh_to_job', 'condor_ssh_to_job', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_stats', 'condor_stats', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_status', 'condor_status', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_store_cred', 'condor_store_cred', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_submit_dag', 'condor_submit_dag', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_submit', 'condor_submit', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_suspend', 'condor_suspend', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_tail', 'condor_tail', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_top', 'condor_top', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_transfer_data', 'condor_transfer_data', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_transform_ads', 'condor_transform_ads', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_update_machine_ad', 'condor_update_machine_ad', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_updates_stats', 'condor_updates_stats', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_urlfetch', 'condor_urlfetch', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_userlog', 'condor_userlog', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_userprio', 'condor_userprio', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_vacate_job', 'condor_vacate_job', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_vacate', 'condor_vacate', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_version', 'condor_version', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_wait', 'condor_wait', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/condor_who', 'condor_who', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/gidd_alloc', 'gidd_alloc', u'HTCondor Manual', [u'HTCondor Team'], 1),
    ('man-pages/procd_ctl', 'procd_ctl', u'HTCondor Manual', [u'HTCondor Team'], 1)
]

# If true, show URL addresses after external links.
# man_show_urls = False


# -- Options for Texinfo output -------------------------------------------

# Grouping the document tree into Texinfo files. List of tuples
# (source start file, target name, title, author,
#  dir menu entry, description, category)
texinfo_documents = [
    ('index', 'HTCondorManual', u'HTCondor Manual',
     u'HTCondor Team', 'HTCondorManual', 'HTCondor Project',
     'Miscellaneous'),
]

# Documents to append as an appendix to all manuals.
# texinfo_appendices = []

# If false, no module index is generated.
# texinfo_domain_indices = True

# How to display URL addresses: 'footnote', 'no', or 'inline'.
# texinfo_show_urls = 'footnote'

# If true, do not generate a @detailmenu in the "Top" node's menu.
# texinfo_no_detailmenu = False

# intersphinx
intersphinx_mapping = {'python': ('https://docs.python.org/3', None)}

# autodoc settings
autoclass_content = 'both'

# napoleon settings
napoleon_use_param = False


def modify_docstring(app, what, name, obj, options, lines):
    """
    Hook function that has a chance to modify whatever comes out of autodoc.

    Parameters
    ----------
    app
        The Sphinx application object
    what
        The type of the object which the docstring belongs to
        "module", "class", "exception", "function", "method", "attribute"
    name
        The fully qualified name of the object
    obj
        The object itself
    options
        The autodoc options
    lines
        The actual lines: modify in-place!
    """
    # strip trailing C++ signature text
    for i, line in enumerate(lines):
        if 'C++ signature :' in line:
            for _ in range(len(lines) - i):
                lines.pop()
            break

    # this is Boost's dumb way of saying an object has no __init__
    for i, line in enumerate(lines):
        if line == 'Raises an exception':
            lines[i] = ''
            lines[i + 1] = ''

    # strip leading spaces
    if len(lines) > 0:
        first_indent_len = len(lines[0]) - len(lines[0].lstrip())
        for i, line in enumerate(lines):
            if len(line) > first_indent_len:
                lines[i] = line[first_indent_len:]


remove_types_from_signatures = re.compile(r' \([^)]*\)')
remove_trailing_brackets = re.compile(r']*\)$')
cleanup_commas = re.compile(r'\s*,\s*')


def modify_signature(app, what, name, obj, options, signature, return_annotation):
    """
    Hook function that has a chance to modify whatever comes out of autodoc.

    Parameters
    ----------
    app
        The Sphinx application object
    what
        The type of the object which the docstring belongs to
        "module", "class", "exception", "function", "method", "attribute"
    name
        The fully qualified name of the object
    obj
        The object itself
    options
        The autodoc options
    signature
        the function signature, of the form "(parameter_1, parameter_2)"
        or None if there was no return annotation
    return_annotation
        the function return annotation, of the form
        " -> annotation", or None if there is no return annotation

    Returns
    -------
    (signature, return_annotation)
    """
    if signature is not None:
        signature = re.sub(remove_types_from_signatures, ' ', signature)
        signature = re.sub(remove_trailing_brackets, ')', signature)
        signature = signature.replace('[,', ',')
        signature = re.sub(cleanup_commas, ', ', signature)
        signature = signature.replace('self', '')
        signature = signature.replace('( ', '(')
        signature = signature.replace('(, ', '(')

    if return_annotation == 'None :' and what == 'class':
        return_annotation = ''

    return signature, return_annotation

def setup(app):
    app.add_stylesheet('css/htcondor-manual.css')
    app.connect('autodoc-process-docstring', modify_docstring)
    app.connect('autodoc-process-signature', modify_signature)
