#include "vulkax/workflow/fidelity.hpp"

#include <stdexcept>

namespace vulkax::workflow {
FidelityRun runFidelityLadder(std::uint64_t ph,std::uint64_t sh,std::string backend,std::string device,const std::vector<FidelityLevel>&levels,const FidelityPolicy&policy,const FidelityExecutor&executor,bool stop){if(levels.empty())throw std::invalid_argument("fidelity ladder cannot be empty");FidelityRun run;run.certificate.problemHash=ph;run.certificate.solverHash=sh;run.certificate.backend=backend;run.certificate.device=device;for(const auto&level:levels){if(level.spacing<=0)throw std::invalid_argument("fidelity spacing must be positive");auto observation=executor(level);observation.level=level;run.observations.push_back(observation);std::vector<verify::ConvergenceSample> samples;for(const auto&o:run.observations)samples.push_back({o.level.spacing,o.observable});run.convergence=verify::estimateRichardson(samples);run.certificate=verify::certificateFromConvergence(ph,sh,backend,device,run.convergence,policy.relativeTolerance,observation.residual,policy.residualTolerance,observation.conservationError,policy.conservationTolerance);double total=0;for(const auto&o:run.observations)total+=o.wallSeconds;run.certificate.wallSeconds=total;if(stop&&run.certificate.state==verify::TrustState::Verified)break;}return run;}
} // namespace vulkax::workflow
