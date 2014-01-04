/* The MIT License

   Copyright (c) 2013 Adrian Tan <atks@umich.edu>

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.
*/

#include "annotate_variants.h"

namespace
{

class Igor : Program
{
    public:

    std::string version;

    ///////////
    //options//
    ///////////
    std::string input_vcf_file;
    std::string ref_fasta_file;
    std::string output_vcf_file;
    std::vector<GenomeInterval> intervals;
    std::string interval_list;

    ///////
    //i/o//
    ///////
    BCFOrderedReader *odr;
    BCFOrderedWriter *odw;

    /////////
    //stats//
    /////////
    uint32_t no_variants_annotated;

    ////////////////
    //common tools//
    ////////////////
    VariantManip *vm;

    Igor(int argc, char **argv)
    {
        version = "0.5";

        //////////////////////////
        //options initialization//
        //////////////////////////
        try
        {
            std::string desc = "annotates variants in a VCF file";

            TCLAP::CmdLine cmd(desc, ' ', version);
            VTOutput my; cmd.setOutput(&my);
            TCLAP::ValueArg<std::string> arg_intervals("i", "i", "intervals", false, "", "str", cmd);
            TCLAP::ValueArg<std::string> arg_interval_list("I", "I", "file containing list of intervals []", false, "", "str", cmd);
            TCLAP::ValueArg<std::string> arg_output_vcf_file("o", "o", "output VCF file [-]", false, "-", "str", cmd);
            TCLAP::ValueArg<std::string> arg_ref_fasta_file("r", "r", "reference sequence fasta file []", false, "", "str", cmd);
            TCLAP::UnlabeledValueArg<std::string> arg_input_vcf_file("<in.vcf>", "input VCF file", true, "","file", cmd);

            cmd.parse(argc, argv);

            input_vcf_file = arg_input_vcf_file.getValue();
            output_vcf_file = arg_output_vcf_file.getValue();
            parse_intervals(intervals, arg_interval_list.getValue(), arg_intervals.getValue());
            parse_intervals(intervals, arg_interval_list.getValue(), arg_intervals.getValue());
            ref_fasta_file   = arg_ref_fasta_file.getValue();

        }
        catch (TCLAP::ArgException &e)
        {
            std::cerr << "error: " << e.error() << " for arg " << e.argId() << std::endl;
            abort();
        }
    };

    ~Igor() {};

    void initialize()
    {
        //******************
        //i/o initialization
        //******************
        odr = new BCFOrderedReader(input_vcf_file, intervals);
        odw = new BCFOrderedWriter(output_vcf_file);
        odw->link_hdr(odr->hdr);
        bcf_hdr_append(odw->hdr, "##INFO=<ID=VT,Number=1,Type=String,Description=\"Variant Type - SNP, MNP, INDEL, CLUMPED\">");
        bcf_hdr_append(odw->hdr, "##INFO=<ID=RU,Number=1,Type=String,Description=\"Repeat unit in a STR or Homopolymer\">");
        bcf_hdr_append(odw->hdr, "##INFO=<ID=RL,Number=1,Type=Integer,Description=\"Repeat Length\">");
//        bcf_hdr_append(odw->hdr, "##INFO=<ID=NS,Number=0,Type=Flag,Description=\"Near to STR\">");
//        bcf_hdr_append(odw->hdr, "##INFO=<ID=FS,Number=0,Type=Flag,Description=\"Frameshift INDEL\">");
//        bcf_hdr_append(odw->hdr, "##INFO=<ID=NFS,Number=0,Type=Flag,Description=\"Non Frameshift INDEL\">");

        ///////////////////////
        //tool initialization//
        ///////////////////////
        vm = new VariantManip(ref_fasta_file);

        ////////////////////////
        //stats initialization//
        ////////////////////////
        no_variants_annotated = 0;
    }

    void print_options()
    {
        std::clog << "annotate_indels v" << version << "\n";
        std::clog << "\n";
        std::clog << "options:     input VCF file(s)     " << input_vcf_file << "\n";
        std::clog << "         [o] output VCF file       " << output_vcf_file << "\n";
        print_ref_op("         [r] ref FASTA file        ", ref_fasta_file);
        print_int_op("         [i] intervals             ", intervals);
        std::clog << "\n";
    }

    void print_stats()
    {
        std::clog << "\n";
        std::cerr << "stats: no. of variants annotated     " << no_variants_annotated << "\n";
        std::clog << "\n";
    }

    void annotate_variants()
    {
        odw->write_hdr();

        bcf1_t *v = odw->get_bcf1_from_pool();
        Variant variant;
        kstring_t s = {0,0,0};
        while (odr->read(v))
        {
            bcf_unpack(v, BCF_UN_STR);
            int32_t vtype = vm->classify_variant(odr->hdr, v, variant);
            //int32_t vtype = vm->classify_variant(odr->hdr, v, variant);

            vm->vtype2string(vtype, &s);
            if (s.l)
            {    
                bcf_update_info_string(odr->hdr, v, "VT", s.s);
            }
            
            if (vtype==VT_SNP)
            {
                //do snp stuff
            }    
            else if (vtype==VT_INDEL)
            {
                //do indel stuff
            }
            
            std::string ru = "ACGT";
            int32_t rl = 4;
            
            bcf_update_info_string(odr->hdr, v, "RU", ru.c_str());
            bcf_update_info_int32(odr->hdr, v, "RL", &rl, 1);
            
            ++no_variants_annotated;
            odw->write(v);
            v = odw->get_bcf1_from_pool();
        }
        
        odw->close();
    };
    
    private:

};
}

void annotate_variants(int argc, char ** argv)
{
    Igor igor(argc, argv);
    igor.print_options();
    igor.initialize();
    igor.annotate_variants();
    igor.print_stats();
};