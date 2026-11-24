/*
 * SCHUIMM: FreeType module list (replaces include/freetype/config/ftmodule.h through
 * FT_CONFIG_MODULES_H). Only what the engine needs for TrueType-outline fonts (.ttf, .otf with
 * TrueType outlines, .ttc): the TrueType driver, the SFNT container, the anti-aliasing
 * rasteriser, the auto-hinter and PostScript glyph names (used by the TrueType driver).
 */
FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
