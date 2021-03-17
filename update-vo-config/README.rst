=====================
VO configuration tool
=====================

The **update-vo-config** tool aims to provide a convenient tool to manage the
VO-related templates for `Quattor <https://www.quattor.org/>`_.


Requirements
============

This tool requires :

* Python 3
* python-openssl


Installation
============

The installation is as simple as:

.. code-block:: console

   $ python3 setup.py install

In case you are using conda, just configure it:

.. code-block:: console

   $ conda create -n quattor urllib3 openssl
   $ conda activate quattor


Usage
=====

The **update-vo-config** tool manage VO-related templates in the following
directories:

* ``basedir/vo/certs``
* ``basedir/vo/params``
* ``basedir/vo/site``

It creates these directories if they are missing and filled them with the
template describing the VOMS servers and VOs configuration obtained from
the `Operation Portal <https://operations-portal.egi.eu/>`_.

Two arguments are mandatory:

* *basedir* - the base directory containing the VO-related files
* *token* - a token used for authenticating against the Operations
  Portal

To run the tool, execute:

.. code-block:: console

   $ python3 update-vo-config -b cfg/sites/example -t d0372805abb4

**Note:** Since March 2021, the new API of the Operaions Portal required
to use a token for accessing the VO-related data. You need first to
authentified against the `Operations Portal API page <https://operations-portal.egi.eu/api-documentation>`_
and ask for a token that can be use with this software.


License
=======

The **update-vo-config** tool is released under the Apache License, Version 2.0.


Hacking
=======

The source code is hosted on the `Quattor Github project <https://github.com/quattor/tools/update-vo-config>`_.

Issues are managed through the `Github ticketing system <https://github.com/quattor/tools/issues>`_.

If you want to provide code fixes or enhancement, please follow the `PEP 8
style guidelines <https://www.python.org/dev/peps/pep-0008>`_.
