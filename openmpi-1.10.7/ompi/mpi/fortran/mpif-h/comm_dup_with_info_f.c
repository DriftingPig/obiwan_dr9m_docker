/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2004-2005 The Trustees of Indiana University and Indiana
 *                         University Research and Technology
 *                         Corporation.  All rights reserved.
 * Copyright (c) 2004-2005 The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2004-2005 High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * Copyright (c) 2004-2005 The Regents of the University of California.
 *                         All rights reserved.
 * Copyright (c) 2011-2013 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2013      Los Alamos National Security, LLC. All rights
 *                         reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"

#include "ompi/mpi/fortran/mpif-h/bindings.h"

#if OPAL_HAVE_WEAK_SYMBOLS && OMPI_PROFILE_LAYER
#pragma weak PMPI_COMM_DUP_WITH_INFO = ompi_comm_dup_with_info_f
#pragma weak pmpi_comm_dup_with_info = ompi_comm_dup_with_info_f
#pragma weak pmpi_comm_dup_with_info_ = ompi_comm_dup_with_info_f
#pragma weak pmpi_comm_dup_with_info__ = ompi_comm_dup_with_info_f

#pragma weak PMPI_Comm_dup_with_info_f = ompi_comm_dup_with_info_f
#pragma weak PMPI_Comm_dup_with_info_f08 = ompi_comm_dup_with_info_f
#elif OMPI_PROFILE_LAYER
OMPI_GENERATE_F77_BINDINGS (PMPI_COMM_DUP_WITH_INFO,
                            pmpi_comm_dup_with_info,
                            pmpi_comm_dup_with_info_,
                            pmpi_comm_dup_with_info__,
                            pompi_comm_dup_with_info_f,
                            (MPI_Fint *comm, MPI_Fint *info, MPI_Fint *newcomm, MPI_Fint *ierr),
                            (comm, info, newcomm, ierr) )
#endif

#if OPAL_HAVE_WEAK_SYMBOLS
#pragma weak MPI_COMM_DUP_WITH_INFO = ompi_comm_dup_with_info_f
#pragma weak mpi_comm_dup_with_info = ompi_comm_dup_with_info_f
#pragma weak mpi_comm_dup_with_info_ = ompi_comm_dup_with_info_f
#pragma weak mpi_comm_dup_with_info__ = ompi_comm_dup_with_info_f

#pragma weak MPI_Comm_dup_with_info_f = ompi_comm_dup_with_info_f
#pragma weak MPI_Comm_dup_with_info_f08 = ompi_comm_dup_with_info_f
#endif

#if ! OPAL_HAVE_WEAK_SYMBOLS && ! OMPI_PROFILE_LAYER
OMPI_GENERATE_F77_BINDINGS (MPI_COMM_DUP_WITH_INFO,
                            mpi_comm_dup_with_info,
                            mpi_comm_dup_with_info_,
                            mpi_comm_dup_with_info__,
                            ompi_comm_dup_with_info_f,
                            (MPI_Fint *comm, MPI_Fint *info, MPI_Fint *newcomm, MPI_Fint *ierr),
                            (comm, info, newcomm, ierr) )
#endif


#if OMPI_PROFILE_LAYER && ! OPAL_HAVE_WEAK_SYMBOLS
#include "ompi/mpi/fortran/mpif-h/profile/defines.h"
#endif

void ompi_comm_dup_with_info_f(MPI_Fint *comm, MPI_Fint *info, MPI_Fint *newcomm, MPI_Fint *ierr)
{
    int c_ierr;
    MPI_Comm c_newcomm;
    MPI_Comm c_comm = MPI_Comm_f2c(*comm);
    MPI_Info c_info;

    c_info = MPI_Info_f2c(*info);

    c_ierr = MPI_Comm_dup_with_info(c_comm, c_info, &c_newcomm);
    if (NULL != ierr) *ierr = OMPI_INT_2_FINT(c_ierr);

    if (MPI_SUCCESS == c_ierr) {
        *newcomm = MPI_Comm_c2f(c_newcomm);
    }
}
