.. _install-from-source-label:

Install from Source
===================

This page gives instructions on how to build and install AutoSA from scratch.
It consists of two steps.

* `Step 1: Install the Prerequisites`_
* `Step 2: Compile AutoSA`_

Step 1: Install the Prerequisites
---------------------------------
Below we list the detailed instructions about installing the prerequisites of AutoSA.

Additionally, you could take a look at our `Dockerfile <https://github.com/UCLA-VAST/AutoSA/blob/master/Dockerfile>`_ for building the Docker image 
of AutoSA for reference instructions to build all the prerequisites on Ubuntu.

PPCG
^^^^

AutoSA is developed upon PPCG (`link <https://repo.or.cz/ppcg.git>`_).
Below are the requirements of PPCG. 

* automake, autoconf, libtool (not needed when compiling a release)
* pkg-config (http://www.freedesktop.org/wiki/Software/pkg-config) (not needed when compiling a release using the included isl and pet)
* gmp (http://gmplib.org/)
* libyaml (http://pyyaml.org/wiki/LibYAML) (only needed if you want to compile the pet executable)
* LLVM/clang libraries, 2.9 or higher (http://clang.llvm.org/get_started.html) Unless you have some other reasons for wanting to use the svn version, it is best to install the latest supported release. For more details, including the latest supported release, see pet/README.

If you are installing on Ubuntu, then you can install the following packages:

.. code:: bash

    automake autoconf libtool pkg-config libgmp3-dev libyaml-dev libclang-dev llvm

Note that you need at least version 3.2 of libclang-dev (ubuntu raring).
Older versions of this package did not include the required libraries.
If you are using an older version of ubuntu, then you need to compile and
install LLVM/clang from source.


Barvinok
^^^^^^^^

AutoSA also uses Barvinok library (`link <http://barvinok.gforge.inria.fr/>`_). 
Below are the requirements of Barvinok.

* NTL (https://libntl.org/)

The detailed instructions for installing NTL can be found at `link <https://libntl.org/doc/tour-unix.html>`_.
Note that NTL needs to be compiled with GMP support, this is, you have to specify

.. code:: bash

    NTL_GMP_LIP=on

NTL also needs to be compiled with ISO mode.   
For versions older than 5.4, this means you need an additional

.. code:: bash

    NTL_STD_CXX=on

Others
^^^^^^

* Python 3.6+ and the corresponding pip.

Step 2: Compile AutoSA
----------------------

After installing the prerequisites, this step will build AutoSA from source.

Get Source from Github
^^^^^^^^^^^^^^^^^^^^^^

Clone the source repo from Github.

.. code:: bash

    git clone https://github.com/UCLA-VAST/AutoSA.git

Run the Installation Script
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Run the installation script to build and install AutoSA.

.. code:: bash

    ./install.sh

After the installation has finished, to test if AutoSA is installed correctly,
you could run the following command to obtain the help information of AutoSA.

.. code:: bash

    ./autosa --help

If the help information is printed on the screen, you are all set and may start to explore 
the magic of AutoSA!    