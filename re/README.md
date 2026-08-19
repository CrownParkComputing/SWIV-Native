# re/ — reverse-engineering notes

Documentation only: object model (OBJECT.md), verb library (VERBS.md), architecture (ARCH.md), porting
guide, handler table and traces seeds.  The disassembly listings and decompiler output are NOT distributed
(they derive from the copyrighted program); regenerate them from your own install with the pipeline
(`~/BattleSquadron-Amiga/tools/recomp_studio.py --project swiv_project.json --stage seed-disasm`, plus
`ira -a -binary -offset=0x20BD20 amprog.bin` for the disk-image listing).
