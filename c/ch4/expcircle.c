//STARTWHOLE
static char help[] = "Newton's method for a two-variable system.\n"
"No analytical Jacobian.  Run with -snes_fd or -snes_mf.\n\n";

#include <petsc.h>

extern PetscErrorCode FormFunction(SNES, Vec, Vec, void*);

int main(int argc,char **argv) {
    PetscErrorCode ierr;
    SNES  snes;          // nonlinear solver
    Vec   x, r;          // solution, residual vectors

    ierr = PetscInitialize(&argc,&argv,NULL,help); if (ierr) return ierr;
    ierr = VecCreate(PETSC_COMM_WORLD,&x); CHKERRQ(ierr);
    ierr = VecSetSizes(x,PETSC_DECIDE,2); CHKERRQ(ierr);
    ierr = VecSetFromOptions(x); CHKERRQ(ierr);
    ierr = VecSet(x,1.0); CHKERRQ(ierr);         // initial iterate
    ierr = VecDuplicate(x,&r); CHKERRQ(ierr);

    ierr = SNESCreate(PETSC_COMM_WORLD,&snes); CHKERRQ(ierr);
    ierr = SNESSetFunction(snes,r,FormFunction,NULL); CHKERRQ(ierr);
    ierr = SNESSetFromOptions(snes); CHKERRQ(ierr);
    ierr = SNESSolve(snes,NULL,x); CHKERRQ(ierr);
    ierr = VecView(x,PETSC_VIEWER_STDOUT_WORLD); CHKERRQ(ierr);

    SNESDestroy(&snes);  VecDestroy(&x);  VecDestroy(&r);
    return PetscFinalize();
}

PetscErrorCode FormFunction(SNES snes, Vec x, Vec F, void *ctx) {
    PetscErrorCode ierr;
    const PetscReal  b = 2.0, *ax;
    PetscReal        *aF;

    ierr = VecGetArrayRead(x,&ax);CHKERRQ(ierr);
    ierr = VecGetArray(F,&aF);CHKERRQ(ierr);
    aF[0] = (1.0 / b) * PetscExpReal(b * ax[0]) - ax[1];
    aF[1] = ax[0] * ax[0] + ax[1] * ax[1] - 1.0;
    ierr = VecRestoreArrayRead(x,&ax);CHKERRQ(ierr);
    ierr = VecRestoreArray(F,&aF);CHKERRQ(ierr);
    return 0;
}
//ENDWHOLE

