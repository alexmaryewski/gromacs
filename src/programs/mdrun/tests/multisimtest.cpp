/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2013,2014,2015,2016,2018 by the GROMACS development team.
 * Copyright (c) 2019,2020,2021, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */

/*! \internal \file
 * \brief
 * Tests for the mdrun multi-simulation functionality
 *
 * \author Mark Abraham <mark.j.abraham@gmail.com>
 * \ingroup module_mdrun_integration_tests
 */
#include "gmxpre.h"

#include "multisimtest.h"

#include <cmath>

#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include "gromacs/mdtypes/md_enums.h"
#include "gromacs/utility/basenetwork.h"
#include "gromacs/utility/gmxassert.h"
#include "gromacs/utility/path.h"
#include "gromacs/utility/real.h"
#include "gromacs/utility/stringutil.h"

#include "testutils/cmdlinetest.h"

#include "moduletest.h"
#include "terminationhelper.h"

namespace gmx
{
namespace test
{

MultiSimTest::MultiSimTest() :
    size_(gmx_node_num()),
    rank_(gmx_node_rank()),
    numRanksPerSimulation_(std::get<0>(GetParam())),
    simulationNumber_(rank_ / numRanksPerSimulation_),
    mdrunCaller_(new CommandLine)

{
    // Zero or less ranks doesn't make sense
    GMX_RELEASE_ASSERT(numRanksPerSimulation_ > 0, "Invalid number of ranks per simulation.");

    const char* directoryNameFormat = "sim_%d";

    // Modify the file manager to have a temporary directory unique to
    // each simulation. No need to have a mutex on this, nobody else
    // can access the fileManager_ yet because we only just
    // constructed it.
    std::string originalTempDirectory = fileManager_.getOutputTempDirectory();
    std::string newTempDirectory =
            Path::join(originalTempDirectory, formatString(directoryNameFormat, simulationNumber_));
    if (rank_ % numRanksPerSimulation_ == 0)
    {
        // Only one rank per simulation creates directory
        Directory::create(newTempDirectory);
    }
#if GMX_LIB_MPI
    // Make sure directories got created.
    MPI_Barrier(MdrunTestFixtureBase::communicator_);
#endif
    fileManager_.setOutputTempDirectory(newTempDirectory);

    mdrunCaller_->append("mdrun");
    mdrunCaller_->addOption("-multidir");
    for (int i = 0; i < size_ / numRanksPerSimulation_; ++i)
    {
        mdrunCaller_->append(Path::join(originalTempDirectory, formatString(directoryNameFormat, i)));
    }
}

bool MultiSimTest::mpiSetupValid() const
{
    // Single simulation case is not implemented in multi-sim
    const bool haveAtLeastTwoSimulations = ((size_ / numRanksPerSimulation_) >= 2);
    // Mdrun will throw error if simulations don't have identical number of ranks
    const bool simulationsHaveIdenticalRankNumber = ((size_ % numRanksPerSimulation_) == 0);

    return (haveAtLeastTwoSimulations && simulationsHaveIdenticalRankNumber);
}

void MultiSimTest::organizeMdpFile(SimulationRunner*    runner,
                                   IntegrationAlgorithm integrator,
                                   TemperatureCoupling  tcoupl,
                                   PressureCoupling     pcoupl,
                                   int                  numSteps) const
{
    GMX_RELEASE_ASSERT(mpiSetupValid(), "Creating the mdp file without valid MPI setup is useless.");
    const real  baseTemperature = 298;
    const real  basePressure    = 1;
    std::string mdpFileContents = formatString(
            "integrator = %s\n"
            "tcoupl = %s\n"
            "pcoupl = %s\n"
            "nsteps = %d\n"
            "nstlog = 1\n"
            "nstcalcenergy = 1\n"
            "tc-grps = System\n"
            "tau-t = 1\n"
            "ref-t = %f\n"
            // pressure coupling (if active)
            "tau-p = 1\n"
            "ref-p = %f\n"
            "compressibility = 4.5e-5\n"
            // velocity generation
            "gen-vel = yes\n"
            "gen-temp = %f\n",
            enumValueToString(integrator),
            enumValueToString(tcoupl),
            enumValueToString(pcoupl),
            numSteps,
            baseTemperature + 0.0001 * rank_,
            basePressure * std::pow(1.01, rank_),
            /* Set things up so that the initial KE decreases with
               increasing replica number, so that the (identical)
               starting PE decreases on the first step more for the
               replicas with higher number, which will tend to force
               replica exchange to occur. */
            std::max(baseTemperature - 10 * rank_, real(0)));
    runner->useStringAsMdpFile(mdpFileContents);
}

void MultiSimTest::runGrompp(SimulationRunner* runner, int numSteps) const
{
    // Call grompp once per simulation
    if (rank_ % numRanksPerSimulation_ == 0)
    {
        const auto& simulator = std::get<1>(GetParam());
        const auto& tcoupl    = std::get<2>(GetParam());
        const auto& pcoupl    = std::get<3>(GetParam());
        organizeMdpFile(runner, simulator, tcoupl, pcoupl, numSteps);
        EXPECT_EQ(0, runner->callGromppOnThisRank());
    }

#if GMX_LIB_MPI
    // Make sure simulation masters have written the .tpr file before other ranks try to read it.
    MPI_Barrier(MdrunTestFixtureBase::communicator_);
#endif
}

void MultiSimTest::runExitsNormallyTest()
{
    if (!mpiSetupValid())
    {
        // Can't test multi-sim without multiple simulations
        return;
    }

    SimulationRunner runner(&fileManager_);
    runner.useTopGroAndNdxFromDatabase("spc2");

    runGrompp(&runner);

    ASSERT_EQ(0, runner.callMdrun(*mdrunCaller_));
}

void MultiSimTest::runMaxhTest()
{
    if (!mpiSetupValid())
    {
        // Can't test multi-sim without multiple simulations
        return;
    }

    SimulationRunner runner(&fileManager_);
    runner.useTopGroAndNdxFromDatabase("spc2");

    TerminationHelper helper(&fileManager_, mdrunCaller_.get(), &runner);
    // Make sure -maxh has a chance to propagate
    int numSteps = 100;
    runGrompp(&runner, numSteps);

    helper.runFirstMdrun(runner.cptFileName_);
    helper.runSecondMdrun();
}

} // namespace test
} // namespace gmx
