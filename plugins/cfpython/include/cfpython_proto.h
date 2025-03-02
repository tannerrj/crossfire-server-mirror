/* cfpython.c */
extern "C" CF_PLUGIN int initPlugin(const char *iversion, f_plug_api gethooksptr);
extern "C" CF_PLUGIN void *getPluginProperty(int *type, ...);
extern "C" void cfpython_runPluginCommand(object *op, const char *params);
extern "C" CF_PLUGIN int postInitPlugin(void);
extern "C" CF_PLUGIN int cfpython_globalEventListener(int *type, ...);
extern "C" CF_PLUGIN int eventListener(int *type, ...);
extern "C" CF_PLUGIN int closePlugin(void);
/* cfpython_archetype.c */
PyObject *Crossfire_Archetype_wrap(archetype *what);
/* cfpython_object.c */
PyObject *Crossfire_Object_wrap(object *what);
/* cfpython_party.c */
PyObject *Crossfire_Party_wrap(partylist *what);
/* cfpython_region.c */
PyObject *Crossfire_Region_wrap(region *what);
/* cfpython_map.c */
void Handle_Map_Unload_Hook(Crossfire_Map *map);
PyObject *Crossfire_Map_wrap(mapstruct *what);
