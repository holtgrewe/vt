/* The MIT License

   Copyright (c) 2014 Adrian Tan <atks@umich.edu>

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

#include "merge.h"

namespace
{

class Igor : Program
{
    public:

    std::string version;

    ///////////
    //options//
    ///////////
    std::vector<std::string> input_vcf_files;
    std::string input_vcf_file_list;
    std::string output_vcf_file;
    std::vector<GenomeInterval> intervals;
    std::string interval_list;
    bool print;
    
    ///////
    //i/o//
    ///////
    BCFSyncedReader *sr;
    BCFOrderedWriter *odw;
    bcf1_t *v;

    ///////////////
    //general use//
    ///////////////
    kstring_t variant;

    /////////
    //stats//
    /////////
    uint32_t no_samples;
    uint32_t no_candidate_snps;
    uint32_t no_candidate_indels;
    uint32_t no_candidate_snpindels;
    uint32_t no_other_variant_types;

    /////////
    //tools//
    /////////
    LogTool *lt;
    VariantManip * vm;

    Igor(int argc, char ** argv)
    {
        //////////////////////////
        //options initialization//
        //////////////////////////
        try
        {
            std::string desc = "Merge VCF. Includes only GT in format field by default.";

            version = "0.5";
            TCLAP::CmdLine cmd(desc, ' ', version);
            VTOutput my; cmd.setOutput(&my);
            TCLAP::ValueArg<std::string> arg_intervals("i", "i", "intervals", false, "", "str", cmd);
            TCLAP::ValueArg<std::string> arg_interval_list("I", "I", "file containing list of intervals []", false, "", "str", cmd);
            TCLAP::ValueArg<std::string> arg_output_vcf_file("o", "o", "output VCF file [-]", false, "-", "", cmd);
            TCLAP::ValueArg<std::string> arg_input_vcf_file_list("L", "L", "file containing list of input VCF files", true, "", "str", cmd);
            TCLAP::SwitchArg arg_print("p", "p", "print options and summary []", cmd, false);
            
            cmd.parse(argc, argv);

            input_vcf_file_list = arg_input_vcf_file_list.getValue();
            output_vcf_file = arg_output_vcf_file.getValue();
            parse_intervals(intervals, arg_interval_list.getValue(), arg_intervals.getValue());
            print = arg_print.getValue();

            ///////////////////////
            //parse input VCF files
            ///////////////////////
            htsFile *file = hts_open(input_vcf_file_list.c_str(), "r");
            if (file==NULL)
            {
                std::cerr << "cannot open " << input_vcf_file_list.c_str() << "\n";
                exit(1);
            }
            kstring_t *s = &file->line;
            while (hts_getline(file, KS_SEP_LINE, s) >= 0)
            {
                if (s->s[0]!='#')
                {
                    input_vcf_files.push_back(std::string(s->s));
                }
            }
            hts_close(file);
        }
        catch (TCLAP::ArgException &e)
        {
            std::cerr << "error: " << e.error() << " for arg " << e.argId() << "\n";
            abort();
        }
    };

    void initialize()
    {
        //////////////////////
        //i/o initialization//
        //////////////////////
        sr = new BCFSyncedReader(input_vcf_files, intervals, false);

        odw = new BCFOrderedWriter(output_vcf_file, 0);
        bcf_hdr_append(odw->hdr, "##fileformat=VCFv4.1");
        bcf_hdr_transfer_contigs(sr->hdrs[0], odw->hdr);
        

        ///////////////
        //general use//
        ///////////////
        variant = {0,0,0};
        no_samples = sr->nfiles;

        ////////////////////////
        //stats initialization//
        ////////////////////////
        no_candidate_snps = 0;
        no_candidate_indels = 0;
        no_candidate_snpindels = 0;
        no_other_variant_types = 0;

        /////////
        //tools//
        /////////
        lt = new LogTool();
        vm = new VariantManip();
    }

    void merge()
    {
        int32_t nfiles = sr->get_nfiles();
            
        //get all the sample names
        int32_t no_samples = 0;   
        for (int32_t i=0; i<nfiles; ++i)
        {
            for (int32_t j=0; j<bcf_hdr_nsamples(sr->hdrs[i]); ++j)
            {
                bcf_hdr_add_sample(odw->hdr, bcf_hdr_get_sample_name(sr->hdrs[i], j));
                ++no_samples;
            }
        }    
        bcf_hdr_append(odw->hdr, "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">");
        
        odw->write_hdr();   
        
        std::vector<bcfptr*> current_recs;
            
        int32_t *cgt = (int32_t*) malloc(no_samples*2*sizeof(int32_t));
        int ncount =0;
        while(sr->read_next_position(current_recs))
        {
            int32_t ngt = 0;
            for (uint32_t i=0; i<current_recs.size(); ++i)
            {
                int32_t file_index = current_recs[i]->file_index;
                bcf1_t *v = current_recs[i]->v;
                bcf_hdr_t *h = current_recs[i]->h;
    
                int8_t *gt = NULL;
                int32_t n = 0;
                int k = bcf_get_genotypes(h, v, &gt, &n); //as a string
                    
                for (int32_t j=0; j<bcf_hdr_nsamples(h); ++j)
                {
                    int a = bcf_gt_allele(gt[j*2]);
                    int b = bcf_gt_allele(gt[j*2+1]);

                    cgt[ngt*2] = gt[j*2];
                    cgt[ngt*2+1] = gt[j*2+1];
                        
                    ++ngt;
                }

                free(gt);
            }
            
            bcf1_t *v = current_recs[0]->v;
            bcf_hdr_t *h = current_recs[0]->h;
            bcf1_t *nv = odw->get_bcf1_from_pool();
            bcf_set_chrom(odw->hdr, nv, bcf_get_chrom(h, v));
            bcf_set_pos1(nv, bcf_get_pos1(v));
            bcf_update_alleles(odw->hdr, nv, const_cast<const char**>(bcf_get_allele(v)), bcf_get_n_allele(v));
            bcf_set_n_sample(nv, no_samples);
            
            bcf_update_genotypes(odw->hdr,nv,cgt,ngt*2);
            odw->write(nv);
        }
        
        free(cgt);
        
        sr->close();
        odw->close();
    };

    void print_options()
    {
        if (!print) return;
        
        std::clog << "merge v" << version << "\n\n";
        std::clog << "options: [L] input VCF file list   " << input_vcf_file_list << " (" << input_vcf_files.size() << " files)\n";
        std::clog << "         [o] output VCF file       " << output_vcf_file << "\n";
        print_int_op("         [i] intervals             ", intervals);
        std::clog << "\n";
    }

    void print_stats()
    {
        if (!print) return;
        
        std::clog << "\n";
        std::cerr << "stats: Total Number of Candidate SNPs                 " << no_candidate_snps << "\n";
        std::clog << "\n";
    };

    ~Igor() {};

    private:
};

}

bool merge(int argc, char ** argv)
{
    Igor igor(argc, argv);
    igor.print_options();
    igor.initialize();
    igor.merge();
    igor.print_stats();
    return igor.print;
}
