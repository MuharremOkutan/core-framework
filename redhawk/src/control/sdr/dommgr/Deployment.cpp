/*
 * This file is protected by Copyright. Please refer to the COPYRIGHT file 
 * distributed with this source distribution.
 * 
 * This file is part of REDHAWK core.
 * 
 * REDHAWK core is free software: you can redistribute it and/or modify it 
 * under the terms of the GNU Lesser General Public License as published by the 
 * Free Software Foundation, either version 3 of the License, or (at your 
 * option) any later version.
 * 
 * REDHAWK core is distributed in the hope that it will be useful, but WITHOUT 
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License 
 * for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License 
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 */

#include <boost/filesystem.hpp>

#include "Deployment.h"

using namespace ossie;
namespace fs = boost::filesystem;

SoftpkgDeployment::SoftpkgDeployment(SoftpkgInfo* softpkg, const ImplementationInfo* implementation) :
    softpkg(softpkg),
    implementation(implementation)
{
}

SoftpkgDeployment::~SoftpkgDeployment()
{
    for (DeploymentList::iterator dependency = dependencies.begin(); dependency != dependencies.end(); ++dependency) {
        delete (*dependency);
    }
}

SoftpkgInfo* SoftpkgDeployment::getSoftpkg()
{
    return softpkg;
}

const ImplementationInfo* SoftpkgDeployment::getImplementation() const
{
    return implementation;
}

void SoftpkgDeployment::addDependency(SoftpkgDeployment* dependency)
{
    dependencies.push_back(dependency);
}

const std::vector<SoftpkgDeployment*>& SoftpkgDeployment::getDependencies()
{
    return dependencies;
}

std::vector<std::string> SoftpkgDeployment::getDependencyLocalFiles()
{
    std::vector<std::string> files;
    for (DeploymentList::iterator dependency = dependencies.begin(); dependency != dependencies.end(); ++dependency) {
        std::vector<std::string> depfiles = (*dependency)->getDependencyLocalFiles();
        std::copy(depfiles.begin(), depfiles.end(), std::back_inserter(files));
        files.push_back((*dependency)->getLocalFile());
    }
    return files;
}

std::string SoftpkgDeployment::getLocalFile()
{
    fs::path codeLocalFile = fs::path(implementation->getLocalFileName());
    if (!codeLocalFile.has_root_directory()) {
        // Path is relative to SPD file location
        fs::path base_dir = fs::path(softpkg->getSpdFileName()).parent_path();
        codeLocalFile = base_dir / codeLocalFile;
    }
    codeLocalFile = codeLocalFile.normalize();
    if (codeLocalFile.has_leaf() && codeLocalFile.leaf() == ".") {
        codeLocalFile = codeLocalFile.branch_path();
    }

    return codeLocalFile.string();
}

ComponentDeployment::ComponentDeployment(ComponentInfo* component, ImplementationInfo* implementation) :
    SoftpkgDeployment(component, implementation),
    component(component),
    affinityOptions(component->getAffinityOptions())
{
}

ComponentInfo* ComponentDeployment::getComponent()
{
    return component;
}

void ComponentDeployment::setAssignedDevice(const boost::shared_ptr<DeviceNode>& device)
{
    assignedDevice = device;
}

boost::shared_ptr<DeviceNode> ComponentDeployment::getAssignedDevice()
{
    return assignedDevice;
}

std::string ComponentDeployment::getEntryPoint()
{
    std::string entryPoint = implementation->getEntryPoint();
    if (!entryPoint.empty()) {
        fs::path entryPointPath = fs::path(entryPoint);
        if (!entryPointPath.has_root_directory()) {
            // Path is relative to SPD file location
            fs::path base_dir = fs::path(softpkg->getSpdFileName()).parent_path();
            entryPointPath = base_dir / entryPointPath;
        }
        entryPoint = entryPointPath.normalize().string();
    }
    return entryPoint;
}

redhawk::PropertyMap ComponentDeployment::getOptions()
{
    // Get the options from the softpkg
    redhawk::PropertyMap options(component->getOptions());

    // Get the PRIORITY and STACK_SIZE from the SPD (if available)
    if (implementation->hasStackSize()) {
        // 3.1.3.3.3.3.6
        // The specification says it's supposed to be an unsigned long, but the
        // parser is set to unsigned long long
        options[CF::ExecutableDevice::STACK_SIZE_ID] = implementation->getStackSize();
    }
    if (implementation->hasPriority()) {
        // 3.1.3.3.3.3.7
        // The specification says it's supposed to be an unsigned long, but the
        // parser is set to unsigned long long
        options[CF::ExecutableDevice::PRIORITY_ID] = implementation->getPriority();
    }

    redhawk::PropertyMap affinity = affinityOptions;
    for (redhawk::PropertyMap::const_iterator prop = affinity.begin(); prop != affinity.end(); ++prop) {
        RH_NL_DEBUG("DomainManager", "ComponentDeployment - Affinity Property: directive id:"
                    <<  prop->getId() << "/" <<  prop->getValue().toString());
    }
    if (!nicAssignment.empty()) {
        redhawk::PropertyMap::iterator nic_prop = affinity.find("nic");
        if (nic_prop == affinity.end() || (nic_prop->getId() != nicAssignment)) {
            // No nic directive, or existing directive differs, append this one
            affinity.push_back(redhawk::PropertyType("nic", nicAssignment));
        }
    }

    if (!affinity.empty()) {
        options["AFFINITY"] = affinity;
    }

    return options;
}

void ComponentDeployment::setNicAssignment(const std::string& nic)
{
    nicAssignment = nic;
}

const std::string& ComponentDeployment::getNicAssignment() const
{
    return nicAssignment;
}

redhawk::PropertyMap ComponentDeployment::getAffinityOptionsWithAssignment() const
{
    redhawk::PropertyMap options = affinityOptions;
    for (redhawk::PropertyMap::const_iterator prop = options.begin(); prop != options.end(); ++prop) {
        RH_NL_DEBUG("DomainManager", "ComponentDeployment getAffinityOptionsWithAssignment ... Affinity Property: directive id:"  <<  prop->getId() << "/" <<  prop->getValue().toString());
    }

    if (!nicAssignment.empty()) {
       RH_NL_DEBUG("DomainManager", "ComponentDeployment getAffinityOptionsWithAssignment ... NIC AFFINITY: pol/value "  <<  "nic"  << "/" << nicAssignment);
       options.push_back(redhawk::PropertyType("nic", nicAssignment));
    }

    return options;
}

void ComponentDeployment::mergeAffinityOptions(const CF::Properties& properties)
{
    // Update existing settings with new ones
    affinityOptions.update(properties);
}

const UsesDeviceInfo* ComponentDeployment::getUsesDeviceById(const std::string& usesId)
{
    const UsesDeviceInfo* usesDevice = component->getUsesDeviceById(usesId);
    if (!usesDevice) {
        usesDevice = implementation->getUsesDeviceById(usesId);
    }
    return usesDevice;
}

void ComponentDeployment::setResourcePtr(CF::Resource_ptr resource)
{
    this->resource = CF::Resource::_duplicate(resource);
}

CF::Resource_ptr ComponentDeployment::getResourcePtr() const
{
    return CF::Resource::_duplicate(resource);
}

ApplicationDeployment::ApplicationDeployment()
{
}

ApplicationDeployment::~ApplicationDeployment()
{
    for (ComponentList::iterator comp = components.begin(); comp != components.end(); ++comp) {
        delete *comp;
    }
}

void ApplicationDeployment::addComponentDeployment(ComponentDeployment* deployment)
{
    components.push_back(deployment);
}

const ApplicationDeployment::ComponentList& ApplicationDeployment::getComponentDeployments()
{
    return components;
}

ComponentDeployment* ApplicationDeployment::getComponentDeployment(const std::string& instantiationId)
{
    for (ComponentList::iterator comp = components.begin(); comp != components.end(); ++comp) {
        if (instantiationId == (*comp)->getComponent()->getInstantiationIdentifier()) {
            return *comp;
        }
    }

    return 0;
}

CF::Resource_ptr ApplicationDeployment::lookupComponentByInstantiationId(const std::string& identifier)
{
    ComponentDeployment* deployment = getComponentDeployment(identifier);
    if (deployment) {
        return deployment->getResourcePtr();
    }
    return CF::Resource::_nil();
}
