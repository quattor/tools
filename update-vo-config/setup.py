#!/usr/bin/env python3
# coding=utf-8
#
# Copyright 2018 CNRS and University of Strasbourg
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may
# not use this file except in compliance with the License. You may obtain
# a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations
# under the License.

from setuptools import setup

__author__ = 'Jerome Pansanel'
__email__ = 'jerome.pansanel@iphc.cnrs.fr'
__version__ = '1.0'

setup(
    name='update-vo-config',
    version=__version__,
    description='VO-related templates update tool',
    long_description='''
        The update-vo-config tool permits to retrieve VO details from the
        Operations Portal and to create the corresponding PAN templates.
    ''',
    classifiers=[
        'Development Status :: 5 - Production/Stable',
        'Environment :: Console',
        'Environment :: Other Environment',
        'Programming Language :: Python :: 3',
        ],
    keywords='',
    author=__author__,
    author_email=__email__,
    url='http://github.com/quattor/tools/update-vo-config',
    license='Apache License, Version 2.0',
    python_requires='>=3',
    install_requires=[
        'setuptools',
        'openssl',
        ],
    scripts=['update-vo-config'],
)
