#if !defined(__IO_H)
#define	__IO_H

#include <permonsvm.h>

FLLOP_EXTERN PetscErrorCode DatasetLoad_SVMLight(Mat,Vec,PetscViewer);
FLLOP_EXTERN PetscErrorCode DatasetLoad_HDF5(Mat,Vec,PetscViewer);

#endif
