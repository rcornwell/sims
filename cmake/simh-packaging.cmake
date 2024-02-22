## The default runtime support component/family:
cpack_add_component(runtime_support
    DISPLAY_NAME "Runtime support"
    DESCRIPTION "Required SIMH runtime support (documentation, shared libraries)"
    REQUIRED
)

## Basic documentation for SIMH
install(FILES doc/simh.doc TYPE DOC COMPONENT runtime_support)

cpack_add_component(b5500_family
    DISPLAY_NAME "Burroughs 5500"
    DESCRIPTION "The Burroughs 5500 system simulator. Simulators: b5500"
)
cpack_add_component(gould_family
    DISPLAY_NAME "Gould simulators"
    DESCRIPTION "Gould Systems simulators. Simulators: sel32"
)
cpack_add_component(ibm_family
    DISPLAY_NAME "IBM"
    DESCRIPTION "IBM system simulators. Simulators: i701, i7010, i704, i7070, i7080, i7090, ibm360"
)
cpack_add_component(ict_family
    DISPLAY_NAME "ICT"
    DESCRIPTION "International Computers and Tabulators simulators. Simulators: icl1900"
)
cpack_add_component(pdp10_family
    DISPLAY_NAME "DEC PDP-10 collection"
    DESCRIPTION "DEC PDP-10 architecture simulators and variants. Simulators: pdp10-ka, pdp10-ki, pdp10-kl, pdp10-ks, pdp6"
)
