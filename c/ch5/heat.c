static char help[] =
"Solves time-dependent heat equation in 2D using TS.  Option prefix -ht_.\n"
"Equation is  u_t = D_0 laplacian u + f.  Domain is (0,1) x (0,1).\n"
"Boundary conditions are non-homogeneous Neumann in x and periodic in y.\n"
"Energy is conserved (for these particular conditions/source) and an extra\n"
"'monitor' is demonstrated.  Discretization is by centered finite differences.\n"
"Converts the PDE into a system  X_t = G(t,X) (PETSc type 'nonlinear') by\n"
"method of lines.  Uses backward Euler time-stepping by default.\n";

#include <petsc.h>

typedef struct {
  PetscReal D0;    // conductivity
} HeatCtx;

static PetscReal f_source(PetscReal x, PetscReal y) {
    return 3.0 * PetscExpReal(-25.0 * (x-0.6) * (x-0.6))
               * PetscSinReal(2.0*PETSC_PI*y);
}

static PetscReal gamma_neumann(PetscReal y) {
    return PetscSinReal(6.0 * PETSC_PI * y);
}

extern PetscErrorCode Spacings(DMDALocalInfo*, PetscReal*, PetscReal*);
extern PetscErrorCode EnergyMonitor(TS, PetscInt, PetscReal, Vec, void*);
extern PetscErrorCode FormRHSFunctionLocal(DMDALocalInfo*, PetscReal, PetscReal**,
                                           PetscReal**, HeatCtx*);
extern PetscErrorCode FormRHSJacobianLocal(DMDALocalInfo*, PetscReal, PetscReal**,
                                           Mat, Mat, HeatCtx*);

int main(int argc,char **argv) {
  PetscErrorCode ierr;
  HeatCtx        user;
  TS             ts;
  Vec            u;
  DM             da;
  DMDALocalInfo  info;
  PetscReal      t0, tf;
  PetscBool      monitorenergy = PETSC_FALSE;

  ierr = PetscInitialize(&argc,&argv,NULL,help); if (ierr) return ierr;

  user.D0  = 1.0;
  ierr = PetscOptionsBegin(PETSC_COMM_WORLD, "ht_", "options for heat", ""); CHKERRQ(ierr);
  ierr = PetscOptionsReal("-D0","constant thermal diffusivity",
           "heat.c",user.D0,&user.D0,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsBool("-monitor","also display total heat energy at each step",
           "heat.c",monitorenergy,&monitorenergy,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);

//STARTDMDASETUP
  ierr = DMDACreate2d(PETSC_COMM_WORLD,
      DM_BOUNDARY_NONE, DM_BOUNDARY_PERIODIC, DMDA_STENCIL_STAR,
      5,4,PETSC_DECIDE,PETSC_DECIDE,  // default to hx=hx=0.25 grid
      1,1,                            // degrees of freedom, stencil width
      NULL,NULL,&da); CHKERRQ(ierr);
  ierr = DMSetFromOptions(da); CHKERRQ(ierr);
  ierr = DMSetUp(da); CHKERRQ(ierr);
  ierr = DMCreateGlobalVector(da,&u); CHKERRQ(ierr);
//ENDDMDASETUP

//STARTTSSETUP
  ierr = TSCreate(PETSC_COMM_WORLD,&ts); CHKERRQ(ierr);
  ierr = TSSetProblemType(ts,TS_NONLINEAR); CHKERRQ(ierr);
  ierr = TSSetDM(ts,da); CHKERRQ(ierr);
  ierr = TSSetApplicationContext(ts,&user); CHKERRQ(ierr);
  ierr = DMDATSSetRHSFunctionLocal(da,INSERT_VALUES,
           (DMDATSRHSFunctionLocal)FormRHSFunctionLocal,&user); CHKERRQ(ierr);
  ierr = DMDATSSetRHSJacobianLocal(da,
           (DMDATSRHSJacobianLocal)FormRHSJacobianLocal,&user); CHKERRQ(ierr);
  if (monitorenergy) {
      ierr = TSMonitorSet(ts,EnergyMonitor,&user,NULL); CHKERRQ(ierr);
  }
  ierr = TSSetType(ts,TSBDF); CHKERRQ(ierr);
  ierr = TSSetTime(ts,0.0); CHKERRQ(ierr);
  ierr = TSSetMaxTime(ts,0.1); CHKERRQ(ierr);
  ierr = TSSetTimeStep(ts,0.001); CHKERRQ(ierr);
  ierr = TSSetExactFinalTime(ts,TS_EXACTFINALTIME_MATCHSTEP); CHKERRQ(ierr);
  ierr = TSSetFromOptions(ts);CHKERRQ(ierr);
//ENDTSSETUP

  // report on set up
  ierr = TSGetTime(ts,&t0); CHKERRQ(ierr);
  ierr = TSGetMaxTime(ts,&tf); CHKERRQ(ierr);
  ierr = DMDAGetLocalInfo(da,&info); CHKERRQ(ierr);
  ierr = PetscPrintf(PETSC_COMM_WORLD,
           "solving on %d x %d grid for t0=%g to tf=%g ...\n",
           info.mx,info.my,t0,tf); CHKERRQ(ierr);

  // solve
  ierr = VecSet(u,0.0); CHKERRQ(ierr);   // initial condition
  ierr = TSSolve(ts,u); CHKERRQ(ierr);

  VecDestroy(&u);  TSDestroy(&ts);  DMDestroy(&da);
  return PetscFinalize();
}

PetscErrorCode Spacings(DMDALocalInfo *info, PetscReal *hx, PetscReal *hy) {
    if (hx)  *hx = 1.0 / (PetscReal)(info->mx-1);
    if (hy)  *hy = 1.0 / (PetscReal)(info->my);   // periodic direction
    return 0;
}

//STARTMONITOR
PetscErrorCode EnergyMonitor(TS ts, PetscInt step, PetscReal time, Vec u,
                             void *ctx) {
    PetscErrorCode ierr;
    HeatCtx        *user = (HeatCtx*)ctx;
    PetscReal      lenergy = 0.0, energy, dt, hx, hy, **au;
    PetscInt       i,j;
    MPI_Comm       com;
    DM             da;
    DMDALocalInfo  info;

    ierr = TSGetDM(ts,&da); CHKERRQ(ierr);
    ierr = DMDAGetLocalInfo(da,&info); CHKERRQ(ierr);
    ierr = DMDAVecGetArrayRead(da,u,&au); CHKERRQ(ierr);
    for (j = info.ys; j < info.ys + info.ym; j++) {
        for (i = info.xs; i < info.xs + info.xm; i++) {
            if ((i == 0) || (i == info.mx-1))
                lenergy += 0.5 * au[j][i];
            else
                lenergy += au[j][i];
        }
    }
    ierr = DMDAVecRestoreArrayRead(da,u,&au); CHKERRQ(ierr);
    ierr = Spacings(&info,&hx,&hy); CHKERRQ(ierr);
    lenergy *= hx * hy;
    ierr = PetscObjectGetComm((PetscObject)(da),&com); CHKERRQ(ierr);
    ierr = MPI_Allreduce(&lenergy,&energy,1,MPIU_REAL,MPIU_SUM,com); CHKERRQ(ierr);
    ierr = TSGetTimeStep(ts,&dt); CHKERRQ(ierr);
    ierr = PetscPrintf(PETSC_COMM_WORLD,"  energy = %9.2e     nu = %8.4f\n",
                energy,user->D0*dt/(hx*hy)); CHKERRQ(ierr);
    return 0;
}
//ENDMONITOR

//STARTRHSFUNCTION
PetscErrorCode FormRHSFunctionLocal(DMDALocalInfo *info,
                                    PetscReal t, PetscReal **au,
                                    PetscReal **aG, HeatCtx *user) {
  PetscErrorCode ierr;
  PetscInt   i, j, mx = info->mx;
  PetscReal  hx, hy, x, y, ul, ur, uxx, uyy;

  ierr = Spacings(info,&hx,&hy); CHKERRQ(ierr);
  for (j = info->ys; j < info->ys + info->ym; j++) {
      y = hy * j;
      for (i = info->xs; i < info->xs + info->xm; i++) {
          x = hx * i;
          // apply Neumann b.c.s
          ul = (i == 0) ? au[j][i+1] + 2.0 * hx * gamma_neumann(y)
                        : au[j][i-1];
          ur = (i == mx-1) ? au[j][i-1] : au[j][i+1];
          uxx = (ul - 2.0 * au[j][i]+ ur) / (hx*hx);
          // DMDA is periodic in y
          uyy = (au[j-1][i] - 2.0 * au[j][i]+ au[j+1][i]) / (hy*hy);
          aG[j][i] = user->D0 * (uxx + uyy) + f_source(x,y);
      }
  }
  return 0;
}
//ENDRHSFUNCTION

//STARTRHSJACOBIAN
PetscErrorCode FormRHSJacobianLocal(DMDALocalInfo *info,
                                    PetscReal t, PetscReal **au,
                                    Mat J, Mat P, HeatCtx *user) {
    PetscErrorCode ierr;
    PetscInt         i, j, ncols;
    const PetscReal  D = user->D0;
    PetscReal        hx, hy, hx2, hy2, v[5];
    MatStencil       col[5],row;

    ierr = Spacings(info,&hx,&hy); CHKERRQ(ierr);
    hx2 = hx * hx;  hy2 = hy * hy;
    for (j = info->ys; j < info->ys+info->ym; j++) {
        row.j = j;  col[0].j = j;
        for (i = info->xs; i < info->xs+info->xm; i++) {
            // set up a standard 5-point stencil for the row
            row.i = i;
            col[0].i = i;
            v[0] = - 2.0 * D * (1.0 / hx2 + 1.0 / hy2);
            col[1].j = j-1;  col[1].i = i;    v[1] = D / hy2;
            col[2].j = j+1;  col[2].i = i;    v[2] = D / hy2;
            col[3].j = j;    col[3].i = i-1;  v[3] = D / hx2;
            col[4].j = j;    col[4].i = i+1;  v[4] = D / hx2;
            ncols = 5;
            // if at the boundary, edit the row back to 4 nonzeros
            if (i == 0) {
                ncols = 4;
                col[3].j = j;  col[3].i = i+1;  v[3] = 2.0 * D / hx2;
            } else if (i == info->mx-1) {
                ncols = 4;
                col[3].j = j;  col[3].i = i-1;  v[3] = 2.0 * D / hx2;
            }
            ierr = MatSetValuesStencil(P,1,&row,ncols,col,v,INSERT_VALUES); CHKERRQ(ierr);
        }
    }

    ierr = MatAssemblyBegin(P,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
    ierr = MatAssemblyEnd(P,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
    if (J != P) {
        ierr = MatAssemblyBegin(J,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
        ierr = MatAssemblyEnd(J,MAT_FINAL_ASSEMBLY); CHKERRQ(ierr);
    }
    return 0;
}
//ENDRHSJACOBIAN

