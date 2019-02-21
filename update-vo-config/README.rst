=====================
VO configuration tool
=====================

The update-vo-config tool aims to provide a convenient tool to manage the VO
related templates.


Installation
============

At the command line:

.. code-block:: console

   $ python setup.py install


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

A single argument is mandatory, *basedir*. To run the tool, execute:

.. code-block:: console

   $ update-vo-config -b cfg/sites/example


License
=======

The update-vo-config tool is released under the Apache License, Version 2.0.


Hacking
=======

The source code is hosted on the `Quattor Github project <https://github.com/quattor/tools/update-vo-config>`_.

Issues are managed through the `Github ticketing system <https://github.com/quattor/tools/issues>`_.

If you want to provide code fixes or enhancement, please follow the `PEP 8
style guidelines <https://www.python.org/dev/peps/pep-0008>`_.
