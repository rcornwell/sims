import os
import functools

## Initialize package_info to an empty dictionary here so
## that it's visible to write_packaging().
package_info = {}


class SIMHPackaging:
    def __init__(self, family, install_flag = True) -> None:
        self.family = family
        self.processed = False
        self.install_flag = install_flag

    def was_processed(self) -> bool:
        return self.processed == True
    
    def encountered(self) -> None:
        self.processed = True

class PkgFamily:
    def __init__(self, component_name, display_name, description) -> None:
        self.component_name = component_name
        self.display_name   = display_name
        self.description    = description

    def write_component_info(self, stream, indent) -> None:
        pkg_description = self.description
        if pkg_description[-1] != '.':
            pkg_description += '.'
        sims = []
        for sim, pkg in package_info.items():
            if pkg.family is self and pkg.was_processed():
                sims.append(sim)

        if len(sims) > 0:
            sims.sort()
            pkg_description += " Simulators: " + ', '.join(sims)
            indent0 = ' ' * indent
            indent4 = ' ' * (indent + 4)
            stream.write(indent0 + "cpack_add_component(" + self.component_name + "\n")
            stream.write(indent4 + "DISPLAY_NAME \"" + self.display_name + "\"\n")
            stream.write(indent4 + "DESCRIPTION \"" + pkg_description + "\"\n")
            stream.write(indent0 + ")\n")

    def __lt__(self, obj):
        return self.component_name < obj.component_name
    def __eq__(self, obj):
        return self.component_name == obj.component_name
    def __gt__(self, obj):
        return self.component_name > obj.component_name
    def __hash__(self):
        return hash(self.component_name)

def write_packaging(toplevel_dir) -> None:
    families = set([sim.family for sim in package_info.values()])
    pkging_file = os.path.join(toplevel_dir, 'cmake', 'simh-packaging.cmake')
    print("==== writing {0}".format(pkging_file))
    with open(pkging_file, "w") as stream:
        ## Runtime support family:
        stream.write("""## The default runtime support component/family:
cpack_add_component(runtime_support
    DISPLAY_NAME "Runtime support"
    DESCRIPTION "Required SIMH runtime support (documentation, shared libraries)"
    REQUIRED
)

## Basic documentation for SIMH
install(FILES doc/simh.doc TYPE DOC COMPONENT runtime_support)

""")

        ## Simulators:
        for family in sorted(families):
            family.write_component_info(stream, 0)


default_family = PkgFamily("default_family", "Default SIMH simulator family.",
    """The SIMH simulator collection of historical processors and computing systems that do not belong to
any other simulated system family"""
)
    
pdp10_family = PkgFamily("pdp10_family", "DEC PDP-10 collection",
    """DEC PDP-10 architecture simulators and variants."""
)

b5500_family = PkgFamily("b5500_family", "Burroughs 5500",
    """The Burroughs 5500 system simulator""")

ibm_family = PkgFamily("ibm_family", "IBM",
    """IBM system simulators"""
)

ict_family = PkgFamily("ict_family", "ICT",
    """International Computers and Tabulators simulators""")

gould_family = PkgFamily("gould_family", "Gould simulators",
    """Gould Systems simulators"""
)

package_info["b5500"] = SIMHPackaging(b5500_family)
package_info["i701"] = SIMHPackaging(ibm_family)
package_info["i7010"] = SIMHPackaging(ibm_family)
package_info["i704"] = SIMHPackaging(ibm_family)
package_info["i7070"] = SIMHPackaging(ibm_family)
package_info["i7080"] = SIMHPackaging(ibm_family)
package_info["i7090"] = SIMHPackaging(ibm_family)
package_info["ibm360"] = SIMHPackaging(ibm_family)
package_info["icl1900"] = SIMHPackaging(ict_family)
package_info["pdp10-ka"] = SIMHPackaging(pdp10_family)
package_info["pdp10-ki"] = SIMHPackaging(pdp10_family)
package_info["pdp10-kl"] = SIMHPackaging(pdp10_family)
package_info["pdp10-ks"] = SIMHPackaging(pdp10_family)
package_info["pdp6"] = SIMHPackaging(pdp10_family)
package_info["sel32"] = SIMHPackaging(gould_family)
