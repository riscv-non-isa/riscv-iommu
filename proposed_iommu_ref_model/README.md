# RISC-V IOMMU golden reference model
This code implements the RISC-V IOMMU specification - https://github.com/riscv-non-isa/riscv-iommu - and
is intended to be a golden reference model for the specification.

# Files organization
- libiommu - C files implementing the specification
- libtables - a support library to build page and directory tables
- test - a sample test application illustrating how to invoke and use libiommu and libtables
