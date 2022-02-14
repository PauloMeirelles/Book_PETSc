static const char help[] =
"Solves obstacle problem in 2D using SNESVI.  Option prefix -obs_.\n"
"The obstacle problem is a free boundary problem for the Poisson equation\n"
"in which the solution u(x,y) is constrained to be above the obstacle psi(x,y):\n"
"    - Lap u = f,  u >= psi.\n"
"Equivalently it is a variational inequality (VI), complementarity problem\n"
"(CP), or an inequality-constrained minimization.  The example here is\n"
"on the square (-2,2)^2 and has known exact solution.  Because of the\n"
"constraint, the problem is nonlinear but the code reuses the residual and\n"
"Jacobian evaluation code for the Poisson equation in ch6/.\n\n";

#include <petsc.h>
#include "../ch6/poissonfunctions.h"

// z = psi(x,y) is the hemispherical obstacle, but made C^1 with "skirt" at r=r0
PetscReal psi(PetscReal x, PetscReal y) {
    const PetscReal  r = x * x + y * y,
                     r0 = 0.9,
                     psi0 = PetscSqrtReal(1.0 - r0*r0),
                     dpsi0 = - r0 / psi0;
    if (r <= r0) {
        return PetscSqrtReal(1.0 - r);
    } else {
        return psi0 + dpsi0 * (r - r0);
    }
}

/*  This exact solution solves a 1D radial free-boundary problem for the
Laplace equation, on the interval 0 < r < 2, with hemispherical obstacle
    psi(r) =  / sqrt(1 - r^2),  r < 1
              \ -1,             otherwise
The Laplace equation applies where u(r) > psi(r),
    u''(r) + r^-1 u'(r) = 0
with boundary conditions including free b.c.s at an unknown location r = a:
    u(a) = psi(a),  u'(a) = psi'(a),  u(2) = 0
The solution is  u(r) = - A log(r) + B   on  r > a.  The boundary conditions
can then be reduced to a root-finding problem for a:
    a^2 (log(2) - log(a)) = 1 - a^2
The solution is a = 0.697965148223374 (giving residual 1.5e-15).  Then
A = a^2*(1-a^2)^(-0.5) and B = A*log(2) are as given below in the code.  */
PetscReal u_exact(PetscReal x, PetscReal y) {
    const PetscReal afree = 0.697965148223374,
                    A     = 0.680259411891719,
                    B     = 0.471519893402112;
    PetscReal       r;
    r = PetscSqrtReal(x * x + y * y);
    return (r <= afree) ? psi(x,y)  // active set; on the obstacle
                        : - A * PetscLogReal(r) + B; // solves laplace eqn
}

// boundary conditions from exact solution
PetscReal g_fcn(PetscReal x, PetscReal y, PetscReal z, void *ctx) {
    return u_exact(x,y);
}

// we solve Laplace's equation with f = 0
PetscReal zero(PetscReal x, PetscReal y, PetscReal z, void *ctx) {
    return 0.0;
}

extern PetscErrorCode FormUExact(DMDALocalInfo*, Vec);
extern PetscErrorCode GetActiveSet(SNES, DMDALocalInfo*, Vec, Vec,
                                   PetscInt*, PetscReal*);
extern PetscErrorCode FormBounds(SNES, Vec, Vec);

int main(int argc,char **argv) {
  PetscErrorCode ierr;
  DM                  da, da_after;
  SNES                snes;
  KSP                 ksp;
  Vec                 u_initial, u, u_exact, Xl, Xu;
  PoissonCtx          user;
  const PetscReal     aexact = 0.697965148223374;
  SNESConvergedReason reason;
  PetscInt            snesit, kspit;
  PetscReal           error1,errorinf,actarea,exactarea,areaerr;
  DMDALocalInfo       info;
  char                dumpname[256] = "dump.dat";
  PetscBool           dumpbinary = PETSC_FALSE;

  ierr = PetscInitialize(&argc,&argv,NULL,help); if (ierr) return ierr;

  ierr = PetscOptionsBegin(PETSC_COMM_WORLD,"obs_","options to obstacle","");CHKERRQ(ierr);
  ierr = PetscOptionsString("-dump_binary",
           "filename for saving solution AND OBSTACLE in PETSc binary format",
           "obstacle.c",dumpname,dumpname,sizeof(dumpname),&dumpbinary); CHKERRQ(ierr);
  ierr = PetscOptionsEnd();CHKERRQ(ierr);

  ierr = DMDACreate2d(PETSC_COMM_WORLD,
      DM_BOUNDARY_NONE, DM_BOUNDARY_NONE, DMDA_STENCIL_STAR,
      3,3,                       // override with -da_grid_x,_y
      PETSC_DECIDE,PETSC_DECIDE, // num of procs in each dim
      1,1,NULL,NULL,             // dof = 1 and stencil width = 1
      &da);CHKERRQ(ierr);
  ierr = DMSetFromOptions(da); CHKERRQ(ierr);
  ierr = DMSetUp(da); CHKERRQ(ierr);
  ierr = DMDASetUniformCoordinates(da,-2.0,2.0,-2.0,2.0,-1.0,-1.0);CHKERRQ(ierr);

  user.cx = 1.0;
  user.cy = 1.0;
  user.cz = 1.0;
  user.g_bdry = &g_fcn;
  user.f_rhs = &zero;
  user.addctx = NULL;
  ierr = DMSetApplicationContext(da,&user);CHKERRQ(ierr);

  ierr = SNESCreate(PETSC_COMM_WORLD,&snes);CHKERRQ(ierr);
  ierr = SNESSetDM(snes,da);CHKERRQ(ierr);
  ierr = SNESSetApplicationContext(snes,&user);CHKERRQ(ierr);

  // set the SNES type to a variational inequality (VI) solver of reduced-space
  // (RS) type
  ierr = SNESSetType(snes,SNESVINEWTONRSLS);CHKERRQ(ierr);
  ierr = SNESVISetComputeVariableBounds(snes,&FormBounds);CHKERRQ(ierr);

  // reuse residual and jacobian from ch6/
  ierr = DMDASNESSetFunctionLocal(da,INSERT_VALUES,
             (DMDASNESFunction)Poisson2DFunctionLocal,&user); CHKERRQ(ierr);
  ierr = DMDASNESSetJacobianLocal(da,
             (DMDASNESJacobian)Poisson2DJacobianLocal,&user); CHKERRQ(ierr);
  ierr = SNESGetKSP(snes,&ksp); CHKERRQ(ierr);
  ierr = KSPSetType(ksp,KSPCG); CHKERRQ(ierr);
  ierr = SNESSetFromOptions(snes);CHKERRQ(ierr);

  // initial iterate is zero for simplicity
  ierr = DMCreateGlobalVector(da,&u_initial);CHKERRQ(ierr);
  ierr = VecSet(u_initial,0.0); CHKERRQ(ierr);

  /* solve and get solution, DM after solution*/
  ierr = SNESSolve(snes,NULL,u_initial);CHKERRQ(ierr);
  ierr = VecDestroy(&u_initial); CHKERRQ(ierr);
  ierr = DMDestroy(&da); CHKERRQ(ierr);
  ierr = SNESGetDM(snes,&da_after); CHKERRQ(ierr);
  ierr = SNESGetSolution(snes,&u); CHKERRQ(ierr); /* do not destroy u */
  ierr = DMDAGetLocalInfo(da_after,&info); CHKERRQ(ierr);
  ierr = VecDuplicate(u,&Xl); CHKERRQ(ierr);
  ierr = VecDuplicate(u,&Xu); CHKERRQ(ierr);
  ierr = FormBounds(snes,Xl,Xu); CHKERRQ(ierr);

  /* save solution to binary file if requested */
  if (dumpbinary) {
      PetscViewer dumpviewer;
      ierr = PetscPrintf(PETSC_COMM_WORLD,
               "writing u,psi in binary format to %s ...\n",dumpname); CHKERRQ(ierr);
      ierr = PetscViewerBinaryOpen(PETSC_COMM_WORLD,dumpname,FILE_MODE_WRITE,&dumpviewer); CHKERRQ(ierr);
      ierr = VecView(u,dumpviewer); CHKERRQ(ierr);
      ierr = VecView(Xl,dumpviewer); CHKERRQ(ierr);
      ierr = PetscViewerDestroy(&dumpviewer); CHKERRQ(ierr);
  }

  /* compute final performance measures */
  ierr = SNESGetConvergedReason(snes,&reason); CHKERRQ(ierr);
  if (reason <= 0) {
      ierr = PetscPrintf(PETSC_COMM_WORLD,
          "WARNING: SNES not converged ... use -snes_converged_reason to check\n"); CHKERRQ(ierr);
  }
  ierr = SNESGetIterationNumber(snes,&snesit); CHKERRQ(ierr);
  ierr = SNESGetKSP(snes,&ksp); CHKERRQ(ierr);
  ierr = KSPGetIterationNumber(ksp,&kspit); CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,
      "done on %d x %d grid ... %s, SNES iters = %d, last KSP iters = %d\n",
      info.mx,info.my,SNESConvergedReasons[reason],snesit,kspit); CHKERRQ(ierr);

  /* compare to exact */
  ierr = GetActiveSet(snes,&info,u,Xl,NULL,&actarea); CHKERRQ(ierr);
  exactarea = PETSC_PI * aexact * aexact;
  areaerr = PetscAbsReal(actarea - exactarea) / exactarea;
  ierr = VecDuplicate(u,&u_exact); CHKERRQ(ierr);
  ierr = FormUExact(&info,u_exact); CHKERRQ(ierr);
  ierr = VecAXPY(u,-1.0,u_exact); CHKERRQ(ierr); /* u <- u - u_exact */
  ierr = VecNorm(u,NORM_1,&error1); CHKERRQ(ierr);
  error1 /= (PetscReal)info.mx * (PetscReal)info.my;
  ierr = VecNorm(u,NORM_INFINITY,&errorinf); CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,
      "errors: av |u-uexact| = %.3e, |u-uexact|_inf = %.3e, active area error = %.3f%%\n",
      error1,errorinf,100.0*areaerr); CHKERRQ(ierr);

  VecDestroy(&u_exact);  VecDestroy(&Xl);  VecDestroy(&Xu);
  SNESDestroy(&snes);
  return PetscFinalize();
}


PetscErrorCode GetActiveSet(SNES snes, DMDALocalInfo *info, Vec u, Vec Xl,
                            PetscInt *act, PetscReal *actarea) {
  PetscErrorCode ierr;
  Vec              F;
  const PetscReal  *au, *aXl, *aF, zerotol = 1.0e-8;  // see petsc/src/snes/impls/vi/vi.c for value
  PetscReal        dx, dy;
  PetscInt         i,n,lact,gact;

  dx = 4.0 / (PetscReal)(info->mx-1);
  dy = 4.0 / (PetscReal)(info->my-1);
  ierr = VecGetLocalSize(u,&n);CHKERRQ(ierr);
  ierr = VecGetArrayRead(u,&au); CHKERRQ(ierr);
  ierr = VecGetArrayRead(Xl,&aXl); CHKERRQ(ierr);
  ierr = SNESGetFunction(snes,&F,NULL,NULL); CHKERRQ(ierr); /* do not destroy F */
  ierr = VecGetArrayRead(F,&aF); CHKERRQ(ierr);
  lact = 0;
  for (i=0; i<n; i++) {
    if ((au[i] <= aXl[i] + zerotol) && (aF[i] > 0.0))
        lact++;
  }
  ierr = VecRestoreArrayRead(u,&au);CHKERRQ(ierr);
  ierr = VecRestoreArrayRead(Xl,&aXl);CHKERRQ(ierr);
  ierr = VecRestoreArrayRead(F,&aF); CHKERRQ(ierr);
  ierr = MPI_Allreduce(&lact,&gact,1,MPIU_INT,MPIU_SUM,PetscObjectComm((PetscObject)snes));CHKERRQ(ierr);
  if (act) {
      *act = gact;
  }
  if (actarea) {
      *actarea = dx * dy * gact;
  }
  return 0;
}


PetscErrorCode FormUExact(DMDALocalInfo *info, Vec u) {
  PetscErrorCode ierr;
  PetscInt   i,j;
  PetscReal  **au, dx, dy, x, y;
  dx = 4.0 / (PetscReal)(info->mx-1);
  dy = 4.0 / (PetscReal)(info->my-1);
  ierr = DMDAVecGetArray(info->da, u, &au);CHKERRQ(ierr);
  for (j=info->ys; j<info->ys+info->ym; j++) {
    y = -2.0 + j * dy;
    for (i=info->xs; i<info->xs+info->xm; i++) {
      x = -2.0 + i * dx;
      au[j][i] = u_exact(x,y);
    }
  }
  ierr = DMDAVecRestoreArray(info->da, u, &au);CHKERRQ(ierr);
  return 0;
}

//STARTBOUNDS
// for call-back: tell SNESVI we want  psi <= u < +infinity
PetscErrorCode FormBounds(SNES snes, Vec Xl, Vec Xu) {
  PetscErrorCode ierr;
  DM             da;
  DMDALocalInfo  info;
  PetscInt       i, j;
  PetscReal      **aXl, dx, dy, x, y;
  ierr = SNESGetDM(snes,&da);CHKERRQ(ierr);
  ierr = DMDAGetLocalInfo(da,&info); CHKERRQ(ierr);
  dx = 4.0 / (PetscReal)(info.mx-1);
  dy = 4.0 / (PetscReal)(info.my-1);
  ierr = DMDAVecGetArray(da, Xl, &aXl);CHKERRQ(ierr);
  for (j=info.ys; j<info.ys+info.ym; j++) {
    y = -2.0 + j * dy;
    for (i=info.xs; i<info.xs+info.xm; i++) {
      x = -2.0 + i * dx;
      aXl[j][i] = psi(x,y);
    }
  }
  ierr = DMDAVecRestoreArray(da, Xl, &aXl);CHKERRQ(ierr);
  ierr = VecSet(Xu,PETSC_INFINITY);CHKERRQ(ierr);
  return 0;
}
//ENDBOUNDS

