#!/bin/bash
set -e
set +x

# run as
#    ./stokesnonuniform.sh &> stokesnonuniform.txt

# problem is default lid-driven cavity with Dirichlet on whole boundary
# on a refined version of graded.msh generated by ../lidbox.py
# FE method is P^2 x P^1 Taylor-Hood

# do before:
#   $ ./lidbox.py graded.geo
#   $ gmsh -2 graded.geo
#   $ cd study/

# compare stokesopt.sh

REFINE=5   # REFINE=4 gives N=8x10^5, REFINE=5 gives N=3.2x10^6, REFINE=6 gives N=1.3x10^7

for SGMG in "-s_ksp_type minres -schurgmg diag" \
            "-s_ksp_type gmres -schurgmg lower" \
            "-s_ksp_type gmres -schurgmg full"; do
    for SPRE in "-schurpre selfp" \
                "-schurpre mass"; do
        cmd="../stokes.py -mesh ../graded.msh -showinfo -s_ksp_converged_reason ${SGMG} ${SPRE} -refine ${REFINE} -log_view"
        echo $cmd
        rm -f foo.txt
        $cmd &> foo.txt
        'grep' "sizes:" foo.txt
        'grep' "solution norms:" foo.txt
        'grep' "solve converged due to" foo.txt
        'grep' "Flop:  " foo.txt | awk '{print $2}'
        'grep' "Time (sec):" foo.txt | awk '{print $3}'
        echo
    done
done

