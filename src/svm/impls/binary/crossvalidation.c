
#include "binaryimpl.h"

#undef __FUNCT__
#define __FUNCT__ "SVMCrossValidation_Binary"
PetscErrorCode SVMCrossValidation_Binary(SVM svm,PetscReal c_arr[],PetscInt m,PetscReal score[])
{
  CrossValidationType cv_type;

  PetscFunctionBegin;
  TRY( SVMGetCrossValidationType(svm,&cv_type) );
  if (cv_type == CROSS_VALIDATION_KFOLD) {
    TRY( SVMKFoldCrossValidation(svm,c_arr,m,score) );
  } else {
    TRY( SVMStratifiedKFoldCrossValidation(svm,c_arr,m,score) );
  }
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "SVMKFoldCrossValidation_Binary"
PetscErrorCode SVMKFoldCrossValidation_Binary(SVM svm,PetscReal c_arr[],PetscInt m,PetscReal score[])
{
  MPI_Comm    comm;
  SVM         cross_svm;

  QPS         qps;

  Mat         Xt,Xt_training,Xt_test;
  Vec         y,y_training,y_test;

  PetscInt    lo,hi,first,n;
  PetscInt    i,j,k,nfolds;
  PetscReal   s;

  IS          is_training,is_test;

  PetscInt    p;       /* penalty type */
  PetscInt    svm_mod;
  SVMLossType svm_loss;

  ModelScore  model_score;

  PetscBool   info_set;

  PetscFunctionBegin;
  TRY( PetscObjectGetComm((PetscObject) svm,&comm) );
  TRY( SVMGetLossType(svm,&svm_loss) );
  TRY( SVMGetMod(svm,&svm_mod) );
  TRY( SVMGetPenaltyType(svm,&p) );

  /* Create SVM used in cross validation */
  TRY( SVMCreate(PetscObjectComm((PetscObject)svm),&cross_svm) );
  TRY( SVMSetType(cross_svm,SVM_BINARY) );

  /* Set options of SVM used in cross validation */
  TRY( SVMSetMod(cross_svm,svm_mod) );
  TRY( SVMSetLossType(cross_svm,svm_loss) );
  TRY( SVMSetPenaltyType(cross_svm,p) );
  TRY( SVMAppendOptionsPrefix(cross_svm,"cross_") );
  TRY( SVMSetFromOptions(cross_svm) );

  TRY( PetscOptionsHasName(NULL,((PetscObject)cross_svm)->prefix,"-svm_info",&info_set) );

  TRY( SVMGetTrainingDataset(svm,&Xt,&y) );
  TRY( MatGetOwnershipRange(Xt,&lo,&hi) );

  TRY( ISCreate(PetscObjectComm((PetscObject) svm),&is_test) );
  TRY( ISSetType(is_test,ISSTRIDE) );

  TRY( SVMGetCrossValidationScoreType(svm,&model_score) );
  TRY( SVMGetNfolds(svm,&nfolds) );
  for (i = 0; i < nfolds; ++i) {
    if (info_set) {
      TRY( PetscPrintf(comm,"SVM: fold %d of %d\n",i+1,nfolds) );
    }

    first = lo + i - 1;
    if (first < lo) first += nfolds;
    n = (hi + nfolds - first - 1) / nfolds;

    /* Create test dataset */
    TRY( ISStrideSetStride(is_test,n,first,nfolds) );
    TRY( MatCreateSubMatrix(Xt,is_test,NULL,MAT_INITIAL_MATRIX,&Xt_test) );
    TRY( VecGetSubVector(y,is_test,&y_test) );

    /* Create training dataset */
    TRY( ISComplement(is_test,lo,hi,&is_training) );
    TRY( MatCreateSubMatrix(Xt,is_training,NULL,MAT_INITIAL_MATRIX,&Xt_training) );
    TRY( VecGetSubVector(y,is_training,&y_training) );

    TRY( SVMSetTrainingDataset(cross_svm,Xt_training,y_training) );
    TRY( SVMSetTestDataset(cross_svm,Xt_test,y_test) );

    /* Test C values on fold */
    TRY( SVMGetQPS(cross_svm,&qps) );
    qps->max_it = 1e7;
    k = 0;
    for (j = 0; j < m; j += p) {
      TRY( SVMSetPenalty(cross_svm,p,&c_arr[j]) );
      TRY( SVMTrain(cross_svm) );
      TRY( SVMTest(cross_svm) );
      TRY( SVMGetModelScore(cross_svm,model_score,&s) );
      score[k++] += s;
    }
    TRY( SVMReset(cross_svm) );

    TRY( VecRestoreSubVector(y,is_training,&y_training) );
    TRY( MatDestroy(&Xt_training) );
    TRY( VecRestoreSubVector(y,is_test,&y_test) );
    TRY( MatDestroy(&Xt_test) );
    TRY( ISDestroy(&is_training) );
  }

  n = m / p;
  for (i = 0; i < n; ++i) score[i] /= (PetscReal) nfolds;

  TRY( ISDestroy(&is_test) );
  TRY( SVMDestroy(&cross_svm) );
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "SVMFoldVecIdx_Binary_Private"
static PetscErrorCode SVMFoldVecIdx_Binary_Private(IS is_idx,Vec idx,PetscInt nfolds,PetscInt i,IS *is_training,IS *is_test)
{
  PetscInt lo,hi;
  PetscInt first,n;

  IS       is_idx_training;
  Vec      idx_training,idx_test;

  PetscFunctionBegin;
  TRY( VecGetOwnershipRange(idx,&lo,&hi) );

  /* Determine first element of index set (local)*/
  first = lo + i - 1;
  if (first < lo) first += nfolds;
  /* Determine length of index set (local) */
  n = (hi + nfolds - first - 1) / nfolds;

  /* Create index set for training */
  TRY( ISStrideSetStride(is_idx,n,first,nfolds) );
  TRY( VecGetSubVector(idx,is_idx,&idx_test) );
  TRY( ISCreateFromVec(idx_test,is_test) );
  TRY( VecRestoreSubVector(idx,is_idx,&idx_test) );

  /* Create index set for test */
  TRY( ISComplement(is_idx,lo,hi,&is_idx_training) );
  TRY( VecGetSubVector(idx,is_idx_training,&idx_training) );
  TRY( ISCreateFromVec(idx_training,is_training) );
  TRY( VecRestoreSubVector(idx,is_idx_training,&idx_training) );

  /* Free memory */
  TRY( ISDestroy(&is_idx_training) );
  PetscFunctionReturn(0);
}

#undef __FUNCT__
#define __FUNCT__ "SVMStratifiedKFoldCrossValidation_Binary"
PetscErrorCode SVMStratifiedKFoldCrossValidation_Binary(SVM svm,PetscReal c_arr[],PetscInt m,PetscReal score[])
{
  SVM_Binary *svm_binary = (SVM_Binary *) svm->data;

  MPI_Comm    comm;
  SVM         cross_svm;

  QPS         qps;

  Mat         Xt,Xt_training,Xt_test;
  Vec         y,y_training,y_test;
  IS          is_training,is_test;

  IS          is_p,is_n;
  Vec         idx_p,idx_n;

  IS          is_idx_p,is_idx_n;
  IS          is_training_p,is_test_p;
  IS          is_training_n,is_test_n;

  PetscInt    nfolds;
  PetscInt    i,j,k;

  PetscInt    p;
  PetscInt    svm_mod;
  SVMLossType svm_loss;

  PetscBool   info_set;

  PetscReal   s;
  ModelScore  model_score;

  PetscFunctionBegin;
  TRY( PetscObjectGetComm((PetscObject) svm,&comm) );
  TRY( SVMGetLossType(svm,&svm_loss) );
  TRY( SVMGetMod(svm,&svm_mod) );
  TRY( SVMGetPenaltyType(svm,&p) );

  /* Create SVM used in cross validation */
  TRY( SVMCreate(PetscObjectComm((PetscObject)svm),&cross_svm) );
  TRY( SVMSetType(cross_svm,SVM_BINARY) );

  /* Set options of SVM used in cross validation */
  TRY( SVMSetMod(cross_svm,svm_mod) );
  TRY( SVMSetLossType(cross_svm,svm_loss) );
  TRY( SVMSetPenaltyType(cross_svm,p) );
  TRY( SVMAppendOptionsPrefix(cross_svm,"cross_") );
  TRY( SVMSetFromOptions(cross_svm) );

  TRY( PetscOptionsHasName(NULL,((PetscObject)cross_svm)->prefix,"-svm_info",&info_set) );

  TRY( SVMGetTrainingDataset(svm,&Xt,&y) );
  /* Split positive and negative training samples */
  is_p = svm_binary->is_p;
  is_n = svm_binary->is_n;

  /* Create vectors that contain indices of positive and negative samples */
  TRY( VecCreateFromIS(is_p,&idx_p) );
  TRY( VecCreateFromIS(is_n,&idx_n) );

  /* Create stride index sets for folding  */
  TRY( ISCreate(comm,&is_idx_p) );
  TRY( ISSetType(is_idx_p,ISSTRIDE) );
  TRY( ISCreate(comm,&is_idx_n) );
  TRY( ISSetType(is_idx_n,ISSTRIDE) );

  /* Perform cross validation */
  TRY( SVMGetCrossValidationScoreType(svm,&model_score) );
  TRY( SVMGetNfolds(svm,&nfolds) );
  for (i = 0; i < nfolds; ++i) {
    if (info_set) {
      TRY( PetscPrintf(comm,"SVM: fold %d of %d\n",i+1,nfolds) );
    }

    /* Fold positive samples */
    TRY( SVMFoldVecIdx_Binary_Private(is_idx_p,idx_p,nfolds,i,&is_training_p,&is_test_p) );
    /* Fold positive samples */
    TRY( SVMFoldVecIdx_Binary_Private(is_idx_n,idx_n,nfolds,i,&is_training_n,&is_test_n) );

    /* Union indices of positive and negative training samples */
    TRY( ISExpand(is_training_p,is_training_n,&is_training) );
    /* Union indices of positive and negative test samples */
    TRY( ISExpand(is_test_p,is_test_n,&is_test) );

    /* Create training dataset */
    TRY( MatCreateSubMatrix(Xt,is_training,NULL,MAT_INITIAL_MATRIX,&Xt_training) );
    TRY( VecGetSubVector(y,is_training,&y_training) );

    /* Create test dataset */
    TRY( MatCreateSubMatrix(Xt,is_test,NULL,MAT_INITIAL_MATRIX,&Xt_test) );
    TRY( VecGetSubVector(y,is_test,&y_test) );

    TRY( SVMSetTrainingDataset(cross_svm,Xt_training,y_training) );
    TRY( SVMSetTestDataset(cross_svm,Xt_test,y_test) );
    /* Test C values on fold */
    TRY( SVMGetQPS(cross_svm,&qps) );
    qps->max_it = 1e7;
    k = 0;
    for (j = 0; j < m; j += p) {
      TRY( SVMSetPenalty(cross_svm,p,&c_arr[j]) );
      TRY( SVMTrain(cross_svm) );
      TRY( SVMTest(cross_svm) );
      TRY( SVMGetModelScore(cross_svm,model_score,&s) );
      score[k++] += s;
    }
    TRY( SVMReset(cross_svm) );

    /* Free memory */
    TRY( MatDestroy(&Xt_training) );
    TRY( MatDestroy(&Xt_test) );
    TRY( VecRestoreSubVector(y,is_training,&y_training) );
    TRY( VecRestoreSubVector(y,is_test,&y_test) );
    TRY( ISDestroy(&is_training) );
    TRY( ISDestroy(&is_test) );
    TRY( ISDestroy(&is_training_n) );
    TRY( ISDestroy(&is_training_p) );
    TRY( ISDestroy(&is_test_n) );
    TRY( ISDestroy(&is_test_p) );
  }

  k = m / p;
  for (i = 0; i < k; ++i) score[i] /= (PetscReal) nfolds;

  /* Free memory */
  TRY( ISDestroy(&is_idx_n) );
  TRY( ISDestroy(&is_idx_p) );
  TRY( VecDestroy(&idx_n) );
  TRY( VecDestroy(&idx_p) );
  TRY( SVMDestroy(&cross_svm) );
  PetscFunctionReturn(0);
}