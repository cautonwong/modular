SECTIONS
{
  .edge_module_registry : ALIGN(4)
  {
    __start_edge_module_registry = .;
    KEEP(*(SORT_BY_NAME(.edge_module_registry*)))
    __stop_edge_module_registry = .;
  }
}
INSERT AFTER .rodata;
