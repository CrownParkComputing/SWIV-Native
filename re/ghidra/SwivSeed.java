// SwivSeed.java -- disassemble at every traced PC, make functions at every traced call target,
// set A6 = 0x2016DC as a register context, then decompile everything.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.lang.*;
import java.io.*;
import java.math.BigInteger;
import java.nio.file.*;
import java.util.*;

public class SwivSeed extends GhidraScript {
    @Override
    public void run() throws Exception {
        String dir = getScriptArgs()[0];
        AddressFactory af = currentProgram.getAddressFactory();
        Register a6 = currentProgram.getRegister("A6");
        Address min = af.getAddress("0x200000"), max = af.getAddress("0x2fffff");
        currentProgram.getProgramContext().setValue(a6, min, max, BigInteger.valueOf(0x2016DC));
        AddressSet seeds = new AddressSet();
        for (String l : Files.readAllLines(Paths.get(dir, "pcs.txt"))) if (!l.isEmpty()) seeds.add(af.getAddress("0x" + l));
        DisassembleCommand dc = new DisassembleCommand(seeds, null, true);
        dc.applyTo(currentProgram, monitor);
        int nf = 0;
        for (String l : Files.readAllLines(Paths.get(dir, "funcs.txt"))) {
            if (l.isEmpty()) continue;
            Address a = af.getAddress("0x" + l);
            if (getFunctionAt(a) == null && createFunction(a, "sub_" + l) != null) nf++;
        }
        println("seeded functions: " + nf);
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        PrintWriter out = new PrintWriter(Paths.get(dir, "swiv_decomp.c").toFile());
        PrintWriter lst = new PrintWriter(Paths.get(dir, "swiv_funcs.txt").toFile());
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            lst.println(f.getEntryPoint() + " " + f.getName() + " " + f.getBody().getNumAddresses());
            DecompileResults res = decomp.decompileFunction(f, 60, monitor);
            if (res != null && res.getDecompiledFunction() != null) {
                out.println("/* ---- " + f.getName() + " @ " + f.getEntryPoint() + " ---- */");
                out.println(res.getDecompiledFunction().getC());
            }
        }
        out.close(); lst.close(); decomp.dispose();
        println("wrote swiv_decomp.c");
    }
}
