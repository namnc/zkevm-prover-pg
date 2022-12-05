#include "starks.hpp"

void Starks::genProof(void *pAddress, FRIProof &proof, Goldilocks::Element *publicInputs, Steps *steps)
{
    // Initialize vars
    uint64_t numCommited = starkInfo.nCm1;
    Transcript transcript;
    Polinomial evals(N, FIELD_EXTENSION);
    Polinomial xDivXSubXi(NExtended, FIELD_EXTENSION);
    Polinomial xDivXSubWXi(NExtended, FIELD_EXTENSION);
    Polinomial challenges(NUM_CHALLENGES, FIELD_EXTENSION);

    CommitPols cmPols(pAddress, starkInfo.mapDeg.section[eSection::cm1_n]);
    Goldilocks::Element *mem = (Goldilocks::Element *)pAddress;

    Goldilocks::Element *p_q_2ns = &mem[starkInfo.mapOffsets.section[eSection::q_2ns]];
    Goldilocks::Element *p_cm1_2ns = &mem[starkInfo.mapOffsets.section[eSection::cm1_2ns]];
    Goldilocks::Element *p_cm1_n = &mem[starkInfo.mapOffsets.section[eSection::cm1_n]];
    Goldilocks::Element *p_cm2_2ns = &mem[starkInfo.mapOffsets.section[eSection::cm2_2ns]];
    Goldilocks::Element *p_cm2_n = &mem[starkInfo.mapOffsets.section[eSection::cm2_n]];
    Goldilocks::Element *p_cm3_2ns = &mem[starkInfo.mapOffsets.section[eSection::cm3_2ns]];
    Goldilocks::Element *p_cm3_n = &mem[starkInfo.mapOffsets.section[eSection::cm3_n]];
    Goldilocks::Element *p_cm4_2ns = &mem[starkInfo.mapOffsets.section[eSection::cm4_2ns]];
    Goldilocks::Element *q_cm4_2ns = &mem[starkInfo.mapOffsets.section[eSection::cm4_2ns]];
    Goldilocks::Element *f_cm4_2ns = &mem[starkInfo.mapOffsets.section[eSection::cm4_2ns]];

    Polinomial root0(HASH_SIZE, 1);
    Polinomial root1(HASH_SIZE, 1);
    Polinomial root2(HASH_SIZE, 1);
    Polinomial root3(HASH_SIZE, 1);
    

    MerkleTreeGL *treesGL[STARK_C12_A_NUM_TREES];
    treesGL[0] = new MerkleTreeGL(NExtended, starkInfo.mapSectionsN1.section[eSection::cm1_n] + starkInfo.mapSectionsN3.section[eSection::cm1_n] * FIELD_EXTENSION, p_cm1_2ns);
    treesGL[1] = new MerkleTreeGL(NExtended, starkInfo.mapSectionsN.section[eSection::cm2_n], p_cm2_2ns);
    treesGL[2] = new MerkleTreeGL(NExtended, starkInfo.mapSectionsN.section[eSection::cm3_n], p_cm3_2ns);
    treesGL[3] = new MerkleTreeGL(NExtended, starkInfo.mapSectionsN.section[eSection::q_2ns], p_q_2ns);
    treesGL[4] = new MerkleTreeGL((Goldilocks::Element *)pConstTreeAddress);

    //--------------------------------
    // 1.- Calculate p_cm1_2ns
    //--------------------------------
    TimerStart(STARK_STEP_1);
    TimerStart(STARK_STEP_1_LDE_AND_MERKLETREE);
    TimerStart(STARK_STEP_1_LDE);

    ntt.extendPol(p_cm1_2ns, p_cm1_n, NExtended, N, starkInfo.mapSectionsN.section[eSection::cm1_n], p_q_2ns);

    TimerStopAndLog(STARK_STEP_1_LDE);
    TimerStart(STARK_STEP_1_MERKLETREE);

    for (uint i = 0; i < starkInfo.nPublics; i++)
    {
        transcript.put(&publicInputs[i], 1);
    }

    treesGL[0]->merkelize();
    treesGL[0]->getRoot(root0.address());
    std::cout << "MerkleTree rootGL 0: [ " << root0.toString(4) << " ]" << std::endl;
    transcript.put(root0.address(), HASH_SIZE);

    TimerStopAndLog(STARK_STEP_1_MERKLETREE);
    TimerStopAndLog(STARK_STEP_1_LDE_AND_MERKLETREE);
    TimerStopAndLog(STARK_STEP_1);

    //--------------------------------
    // 2.- Caluculate plookups h1 and h2
    //--------------------------------
    TimerStart(STARK_STEP_2);
    transcript.getField(challenges[0]); // u
    transcript.getField(challenges[1]); // defVal
    TimerStart(STARK_STEP_2_CALCULATE_EXPS);

    // Calculate exps
#pragma omp parallel for
    for (uint64_t i = 0; i < N; i++)
    {
        steps->step2prev_first(mem, pConstPols, pConstPols2ns, challenges, x_n, x_2ns, zi, evals, xDivXSubXi, xDivXSubWXi, publicInputs, i);
    }
    TimerStopAndLog(STARK_STEP_2_CALCULATE_EXPS);

    TimerStart(STARK_STEP_2_CALCULATEH1H2_TRANSPOSE);
    Polinomial *transPols = transposeH1H2Columns(pAddress, numCommited);
    TimerStopAndLog(STARK_STEP_2_CALCULATEH1H2_TRANSPOSE);

    TimerStart(STARK_STEP_2_CALCULATEH1H2);
    uint64_t nthreads = starkInfo.puCtx.size();
    if (nthreads == 0)
    {
        nthreads += 1;
    }
    uint64_t buffSize = starkInfo.mapSectionsN.section[eSection::q_2ns] * NExtended;
    uint64_t *mam = (uint64_t *)pAddress;
    uint64_t *buffer = &mam[starkInfo.mapOffsets.section[eSection::q_2ns]];
    uint64_t buffSizeThread = buffSize / nthreads;

#pragma omp parallel for num_threads(nthreads)
    for (uint64_t i = 0; i < starkInfo.puCtx.size(); i++)
    {
        int indx1 = 4 * i;
        if (transPols[indx1 + 2].dim() == 1)
        {
            uint64_t buffSizeThreadValues = 3 * N;
            uint64_t buffSizeThreadKeys = buffSizeThread - buffSizeThreadValues;
            Polinomial::calculateH1H2_opt1(transPols[indx1 + 2], transPols[indx1 + 3], transPols[indx1], transPols[indx1 + 1], i, &buffer[omp_get_thread_num() * buffSizeThread], buffSizeThreadKeys, buffSizeThreadValues);
        }
        else
        {
            assert(transPols[indx1 + 2].dim() == 3);
            uint64_t buffSizeThreadValues = 5 * N;
            uint64_t buffSizeThreadKeys = buffSizeThread - buffSizeThreadValues;
            Polinomial::calculateH1H2_opt3(transPols[indx1 + 2], transPols[indx1 + 3], transPols[indx1], transPols[indx1 + 1], i, &buffer[omp_get_thread_num() * buffSizeThread], buffSizeThreadKeys, buffSizeThreadValues);
        }
    }
    TimerStopAndLog(STARK_STEP_2_CALCULATEH1H2);

    TimerStart(STARK_STEP_2_CALCULATEH1H2_TRANSPOSE_2);
    transposeH1H2Rows(pAddress, numCommited, transPols);
    TimerStopAndLog(STARK_STEP_2_CALCULATEH1H2_TRANSPOSE_2);

    TimerStart(STARK_STEP_2_LDE_AND_MERKLETREE);
    TimerStart(STARK_STEP_2_LDE);

    ntt.extendPol(p_cm2_2ns, p_cm2_n, NExtended, N, starkInfo.mapSectionsN.section[eSection::cm2_n], p_q_2ns);

    TimerStopAndLog(STARK_STEP_2_LDE);
    TimerStart(STARK_STEP_2_MERKLETREE);

    treesGL[1]->merkelize();
    treesGL[1]->getRoot(root1.address());
    std::cout << "MerkleTree rootGL 1: [ " << root1.toString(4) << " ]" << std::endl;
    transcript.put(root1.address(), HASH_SIZE);

    TimerStopAndLog(STARK_STEP_2_MERKLETREE);
    TimerStopAndLog(STARK_STEP_2_LDE_AND_MERKLETREE);
    TimerStopAndLog(STARK_STEP_2);

    //--------------------------------
    // 3.- Compute Z polynomials
    //--------------------------------
    TimerStart(STARK_STEP_3);
    transcript.getField(challenges[2]); // gamma
    transcript.getField(challenges[3]); // betta

    TimerStart(STARK_STEP_3_CALCULATE_EXPS);

#pragma omp parallel for
    for (uint64_t i = 0; i < N; i++)
    {
        steps->step3prev_first(mem, pConstPols, pConstPols2ns, challenges, x_n, x_2ns, zi, evals, xDivXSubXi, xDivXSubWXi, publicInputs, i);
    }
    TimerStopAndLog(STARK_STEP_3_CALCULATE_EXPS);

    TimerStart(STARK_STEP_3_CALCULATE_Z_TRANSPOSE);
    Polinomial *newpols_ = transposeZColumns(pAddress, numCommited);
    TimerStopAndLog(STARK_STEP_3_CALCULATE_Z_TRANSPOSE);

    TimerStart(STARK_STEP_3_CALCULATE_Z);
    u_int64_t numpols = starkInfo.ciCtx.size() + starkInfo.peCtx.size() + starkInfo.puCtx.size();
#pragma omp parallel for
    for (uint64_t i = 0; i < numpols; i++)
    {
        int indx1 = 3 * i;
        Polinomial::calculateZ(newpols_[indx1 + 2], newpols_[indx1], newpols_[indx1 + 1]);
    }
    TimerStopAndLog(STARK_STEP_3_CALCULATE_Z);
    TimerStart(STARK_STEP_3_CALCULATE_Z_TRANSPOSE_2);
    transposeZRows(pAddress, numCommited, newpols_);
    TimerStopAndLog(STARK_STEP_3_CALCULATE_Z_TRANSPOSE_2);

    TimerStart(STARK_STEP_3_LDE_AND_MERKLETREE);
    TimerStart(STARK_STEP_3_LDE);
    ntt.extendPol(p_cm3_2ns, p_cm3_n, NExtended, N, starkInfo.mapSectionsN.section[eSection::cm3_n], p_q_2ns);
    TimerStopAndLog(STARK_STEP_3_LDE);
    TimerStart(STARK_STEP_3_MERKLETREE);
    treesGL[2]->merkelize();
    treesGL[2]->getRoot(root2.address());
    std::cout << "MerkleTree rootGL 2: [ " << root2.toString(4) << " ]" << std::endl;
    transcript.put(root2.address(), HASH_SIZE);
    TimerStopAndLog(STARK_STEP_3_MERKLETREE);
    TimerStopAndLog(STARK_STEP_3_LDE_AND_MERKLETREE);
    TimerStopAndLog(STARK_STEP_3);

    //--------------------------------
    // 4. Compute C Polynomial
    //--------------------------------
    TimerStart(STARK_STEP_4);
    TimerStart(STARK_STEP_4_CALCULATE_EXPS);

    transcript.getField(challenges[4]); // gamma

#pragma omp parallel for
    for (uint64_t i = 0; i < N; i++)
    {
        steps->step4_first(mem, pConstPols, pConstPols2ns, challenges, x_n, x_2ns, zi, evals, xDivXSubXi, xDivXSubWXi, &publicInputs[0], i);
    }
    steps->step4_first(mem, pConstPols, pConstPols2ns, challenges, x_n, x_2ns, zi, evals, xDivXSubXi, xDivXSubWXi, &publicInputs[0], N - 1);

    TimerStopAndLog(STARK_STEP_4_CALCULATE_EXPS);

    TimerStart(STARK_STEP_4_LDE);
    //ntt.extendPol(p_exps_withq_2ns, p_exps_withq_n, NExtended, N, starkInfo.mapSectionsN.section[eSection::exps_withq_n], p_q_2ns);
    TimerStopAndLog(STARK_STEP_4_LDE);

    TimerStart(STARK_STEP_4_CALCULATE_EXPS_2NS);
    uint64_t extendBits = starkInfo.starkStruct.nBitsExt - starkInfo.starkStruct.nBits;

#pragma omp parallel for
    for (uint64_t i = 0; i < NExtended; i++)
    {
        steps->step42ns_first(mem, pConstPols, pConstPols2ns, challenges, x_n, x_2ns, zi, evals, xDivXSubXi, xDivXSubWXi, &publicInputs[0], i);
    }

    TimerStopAndLog(STARK_STEP_4_CALCULATE_EXPS_2NS);
    TimerStart(STARK_STEP_4_MERKLETREE);

    treesGL[3]->merkelize();
    treesGL[3]->getRoot(root3.address());
    std::cout << "MerkleTree rootGL 3: [ " << root3.toString(4) << " ]" << std::endl;
    transcript.put(root3.address(), HASH_SIZE);

    TimerStopAndLog(STARK_STEP_4_MERKLETREE);
    TimerStopAndLog(STARK_STEP_4);

    //--------------------------------
    // 5. Compute FRI Polynomial
    //--------------------------------
    TimerStart(STARK_STEP_5);
    TimerStart(STARK_STEP_5_LEv_LpEv);

    transcript.getField(challenges[5]); // v1
    transcript.getField(challenges[6]); // v2
    transcript.getField(challenges[7]); // xi

    Polinomial LEv(N, 3, "LEv");
    Polinomial LpEv(N, 3, "LpEv");
    Polinomial xis(1, 3);
    Polinomial wxis(1, 3);
    Polinomial c_w(1, 3);

    Goldilocks3::one((Goldilocks3::Element &)*LEv[0]);
    Goldilocks3::one((Goldilocks3::Element &)*LpEv[0]);

    Polinomial::divElement(xis, 0, challenges, 7, (Goldilocks::Element &)Goldilocks::shift());
    Polinomial::mulElement(c_w, 0, challenges, 7, (Goldilocks::Element &)Goldilocks::w(starkInfo.starkStruct.nBits));
    Polinomial::divElement(wxis, 0, c_w, 0, (Goldilocks::Element &)Goldilocks::shift());

    for (uint64_t k = 1; k < N; k++)
    {
        Polinomial::mulElement(LEv, k, LEv, k - 1, xis, 0);
        Polinomial::mulElement(LpEv, k, LpEv, k - 1, wxis, 0);
    }
    ntt.INTT(LEv.address(), LEv.address(), N, 3);
    ntt.INTT(LpEv.address(), LpEv.address(), N, 3);
    TimerStopAndLog(STARK_STEP_5_LEv_LpEv);

    TimerStart(STARK_STEP_5_EVMAP);
    evmap(pAddress, evals, LEv, LpEv);
    TimerStopAndLog(STARK_STEP_5_EVMAP);
    TimerStart(STARK_STEP_5_XDIVXSUB);

    // Calculate xDivXSubXi, xDivXSubWXi
    Polinomial xi(1, FIELD_EXTENSION);
    Polinomial wxi(1, FIELD_EXTENSION);

    Polinomial::copyElement(xi, 0, challenges, 7);
    Polinomial::mulElement(wxi, 0, challenges, 7, (Goldilocks::Element &)Goldilocks::w(starkInfo.starkStruct.nBits));

    Polinomial x(1, FIELD_EXTENSION);
    *x[0] = Goldilocks::shift();

    for (uint64_t k = 0; k < (N << extendBits); k++)
    {
        Polinomial::subElement(xDivXSubXi, k, x, 0, xi, 0);
        Polinomial::subElement(xDivXSubWXi, k, x, 0, wxi, 0);
        Polinomial::mulElement(x, 0, x, 0, (Goldilocks::Element &)Goldilocks::w(starkInfo.starkStruct.nBits + extendBits));
    }

    Polinomial::batchInverse(xDivXSubXi, xDivXSubXi);
    Polinomial::batchInverse(xDivXSubWXi, xDivXSubWXi);

    Polinomial x1(1, FIELD_EXTENSION);
    *x1[0] = Goldilocks::shift();

    for (uint64_t k = 0; k < (N << extendBits); k++)
    {
        Polinomial::mulElement(xDivXSubXi, k, xDivXSubXi, k, x1, 0);
        Polinomial::mulElement(xDivXSubWXi, k, xDivXSubWXi, k, x1, 0);
        Polinomial::mulElement(x1, 0, x1, 0, (Goldilocks::Element &)Goldilocks::w(starkInfo.starkStruct.nBits + extendBits));
    }
    TimerStopAndLog(STARK_STEP_5_XDIVXSUB);
    TimerStart(STARK_STEP_5_CALCULATE_EXPS);

#pragma omp parallel for
    for (uint64_t i = 0; i < NExtended; i++)
    {
        steps->step52ns_first(mem, pConstPols, pConstPols2ns, challenges, x_n, x_2ns, zi, evals, xDivXSubXi, xDivXSubWXi, publicInputs, i);
    }

    TimerStopAndLog(STARK_STEP_5_CALCULATE_EXPS);
    TimerStopAndLog(STARK_STEP_5);
    TimerStart(STARK_STEP_FRI);

    Polinomial friPol = starkInfo.getPolinomial(mem, starkInfo.exps_2ns[starkInfo.friExpId]);
    FRIProve::prove(proof, treesGL, transcript, friPol, starkInfo.starkStruct.nBitsExt, starkInfo);

    proof.proofs.setEvals(evals.address());

    std::memcpy(&proof.proofs.root1[0], root0.address(), HASH_SIZE * sizeof(Goldilocks::Element));
    std::memcpy(&proof.proofs.root2[0], root1.address(), HASH_SIZE * sizeof(Goldilocks::Element));
    std::memcpy(&proof.proofs.root3[0], root2.address(), HASH_SIZE * sizeof(Goldilocks::Element));
    std::memcpy(&proof.proofs.root4[0], root3.address(), HASH_SIZE * sizeof(Goldilocks::Element));
    for (uint i = 0; i < 5; i++)
    {
        delete treesGL[i];
    }
}

Polinomial *Starks::transposeH1H2Columns(void *pAddress, uint64_t &numCommited)
{
    Goldilocks::Element *mem = (Goldilocks::Element *)pAddress;

    u_int64_t stride_pol0 = N * FIELD_EXTENSION + 8;
    uint64_t tot_pols0 = 4 * starkInfo.puCtx.size();
    Polinomial *transPols = new Polinomial[tot_pols0];
    Goldilocks::Element *buffpols0 = &mem[starkInfo.mapOffsets.section[eSection::exps_withq_2ns]];
    assert(starkInfo.mapSectionsN.section[eSection::q_2ns] * NExtended >= 3 * tot_pols0 * N);

    //#pragma omp parallel for
    for (uint64_t i = 0; i < starkInfo.puCtx.size(); i++)
    {
        Polinomial fPol = starkInfo.getPolinomial(mem, starkInfo.exps_n[starkInfo.puCtx[i].fExpId]);
        Polinomial tPol = starkInfo.getPolinomial(mem, starkInfo.exps_n[starkInfo.puCtx[i].tExpId]);
        Polinomial h1 = starkInfo.getPolinomial(mem, starkInfo.cm_n[numCommited + i * 2]);
        Polinomial h2 = starkInfo.getPolinomial(mem, starkInfo.cm_n[numCommited + i * 2 + 1]);

        uint64_t indx = i * 4;
        transPols[indx].potConstruct(&(buffpols0[indx * stride_pol0]), fPol.degree(), fPol.dim(), fPol.dim());
        Polinomial::copy(transPols[indx], fPol);
        indx++;
        transPols[indx].potConstruct(&(buffpols0[indx * stride_pol0]), tPol.degree(), tPol.dim(), tPol.dim());
        Polinomial::copy(transPols[indx], tPol);
        indx++;

        transPols[indx].potConstruct(&(buffpols0[indx * stride_pol0]), h1.degree(), h1.dim(), h1.dim());
        indx++;

        transPols[indx].potConstruct(&(buffpols0[indx * stride_pol0]), h2.degree(), h2.dim(), h2.dim());
    }
    return transPols;
}
void Starks::transposeH1H2Rows(void *pAddress, uint64_t &numCommited, Polinomial *transPols)
{
    Goldilocks::Element *mem = (Goldilocks::Element *)pAddress;

    for (uint64_t i = 0; i < starkInfo.puCtx.size(); i++)
    {
        int indx1 = 4 * i + 2;
        int indx2 = 4 * i + 3;
        Polinomial h1 = starkInfo.getPolinomial(mem, starkInfo.cm_n[numCommited + i * 2]);
        Polinomial h2 = starkInfo.getPolinomial(mem, starkInfo.cm_n[numCommited + i * 2 + 1]);
        Polinomial::copy(h1, transPols[indx1]);
        Polinomial::copy(h2, transPols[indx2]);
    }
    if (starkInfo.puCtx.size() > 0)
    {
        delete[] transPols;
    }
    numCommited = numCommited + starkInfo.puCtx.size() * 2;
}
Polinomial *Starks::transposeZColumns(void *pAddress, uint64_t &numCommited)
{
    Goldilocks::Element *mem = (Goldilocks::Element *)pAddress;

    u_int64_t stride_pol_ = N * FIELD_EXTENSION + 8; // assuming all polinomials have same degree
    uint64_t tot_pols = 3 * (starkInfo.puCtx.size() + starkInfo.peCtx.size() + starkInfo.ciCtx.size());
    Polinomial *newpols_ = (Polinomial *)calloc(tot_pols * sizeof(Polinomial), 1);
    Goldilocks::Element *buffpols_ = &mem[starkInfo.mapOffsets.section[eSection::exps_withq_2ns]];
    assert(starkInfo.mapSectionsN.section[eSection::q_2ns] * NExtended >= tot_pols * stride_pol_);

    if (buffpols_ == NULL || newpols_ == NULL)
    {
        cout << "memory problems!" << endl;
        exit(1);
    }

    //#pragma omp parallel for (better without)
    for (uint64_t i = 0; i < starkInfo.puCtx.size(); i++)
    {
        Polinomial pNum = starkInfo.getPolinomial(mem, starkInfo.exps_n[starkInfo.puCtx[i].numId]);
        Polinomial pDen = starkInfo.getPolinomial(mem, starkInfo.exps_n[starkInfo.puCtx[i].denId]);
        Polinomial z = starkInfo.getPolinomial(mem, starkInfo.cm_n[numCommited + i]);
        u_int64_t indx = i * 3;
        newpols_[indx].potConstruct(&(buffpols_[indx * stride_pol_]), pNum.degree(), pNum.dim(), pNum.dim());
        Polinomial::copy(newpols_[indx], pNum);
        indx++;
        assert(pNum.degree() <= N);

        newpols_[indx].potConstruct(&(buffpols_[indx * stride_pol_]), pDen.degree(), pDen.dim(), pDen.dim());
        Polinomial::copy(newpols_[indx], pDen);
        indx++;
        assert(pDen.degree() <= N);

        newpols_[indx].potConstruct(&(buffpols_[indx * stride_pol_]), z.degree(), z.dim(), z.dim());
        assert(z.degree() <= N);
    }
    numCommited += starkInfo.puCtx.size();
    u_int64_t offset = 3 * starkInfo.puCtx.size();
    for (uint64_t i = 0; i < starkInfo.peCtx.size(); i++)
    {
        Polinomial pNum = starkInfo.getPolinomial(mem, starkInfo.exps_n[starkInfo.peCtx[i].numId]);
        Polinomial pDen = starkInfo.getPolinomial(mem, starkInfo.exps_n[starkInfo.peCtx[i].denId]);
        Polinomial z = starkInfo.getPolinomial(mem, starkInfo.cm_n[numCommited + i]);
        u_int64_t indx = 3 * i + offset;
        newpols_[indx].potConstruct(&(buffpols_[indx * stride_pol_]), pNum.degree(), pNum.dim(), pNum.dim());
        Polinomial::copy(newpols_[indx], pNum);
        indx++;
        assert(pNum.degree() <= N);

        newpols_[indx].potConstruct(&(buffpols_[indx * stride_pol_]), pDen.degree(), pDen.dim(), pDen.dim());
        Polinomial::copy(newpols_[indx], pDen);
        indx++;
        assert(pDen.degree() <= N);

        newpols_[indx].potConstruct(&(buffpols_[indx * stride_pol_]), z.degree(), z.dim(), z.dim());
        assert(z.degree() <= N);
    }
    numCommited += starkInfo.peCtx.size();
    offset += 3 * starkInfo.peCtx.size();
    for (uint64_t i = 0; i < starkInfo.ciCtx.size(); i++)
    {

        Polinomial pNum = starkInfo.getPolinomial(mem, starkInfo.exps_n[starkInfo.ciCtx[i].numId]);
        Polinomial pDen = starkInfo.getPolinomial(mem, starkInfo.exps_n[starkInfo.ciCtx[i].denId]);
        Polinomial z = starkInfo.getPolinomial(mem, starkInfo.cm_n[numCommited + i]);
        u_int64_t indx = 3 * i + offset;

        newpols_[indx].potConstruct(&(buffpols_[indx * stride_pol_]), pNum.degree(), pNum.dim(), pNum.dim());
        Polinomial::copy(newpols_[indx], pNum);
        indx++;
        assert(pNum.degree() <= N);

        newpols_[indx].potConstruct(&(buffpols_[indx * stride_pol_]), pDen.degree(), pDen.dim(), pDen.dim());
        Polinomial::copy(newpols_[indx], pDen);
        indx++;
        assert(pDen.degree() <= N);

        newpols_[indx].potConstruct(&(buffpols_[indx * stride_pol_]), z.degree(), z.dim(), z.dim());
        assert(z.degree() <= N);
    }
    numCommited += starkInfo.ciCtx.size();
    numCommited -= starkInfo.ciCtx.size() + starkInfo.peCtx.size() + starkInfo.puCtx.size();
    return newpols_;
}
void Starks::transposeZRows(void *pAddress, uint64_t &numCommited, Polinomial *transPols)
{
    u_int64_t numpols = starkInfo.ciCtx.size() + starkInfo.peCtx.size() + starkInfo.puCtx.size();
    Goldilocks::Element *mem = (Goldilocks::Element *)pAddress;
    for (uint64_t i = 0; i < numpols; i++)
    {
        int indx1 = 3 * i;
        Polinomial z = starkInfo.getPolinomial(mem, starkInfo.cm_n[numCommited + i]);
        Polinomial::copy(z, transPols[indx1 + 2]);
    }
    if (numpols > 0)
    {
        free(transPols);
    }
}
void Starks::evmap(void *pAddress, Polinomial &evals, Polinomial &LEv, Polinomial &LpEv)
{
    Goldilocks::Element *mem = (Goldilocks::Element *)pAddress;
    uint64_t extendBits = starkInfo.starkStruct.nBitsExt - starkInfo.starkStruct.nBits;
    /* sort polinomials depending on its type

        Subsets:
            0. const
            1. cm , dim=1
            2. qs , dim=1  //1 and 2 to be joined
            3. cm , dim=3
            4. qs, dim=3   //3 and 4 to be joined
     */

    u_int64_t size_eval = starkInfo.evMap.size();
    u_int64_t *sorted_evMap = (u_int64_t *)malloc(5 * size_eval * sizeof(u_int64_t));
    u_int64_t counters[5] = {0, 0, 0, 0, 0};

    for (uint64_t i = 0; i < size_eval; i++)
    {
        EvMap ev = starkInfo.evMap[i];
        if (ev.type == EvMap::eType::_const)
        {
            sorted_evMap[counters[0]] = i;
            ++counters[0];
        }
        else if (ev.type == EvMap::eType::cm)
        {
            uint16_t idPol = (ev.type == EvMap::eType::cm) ? starkInfo.cm_2ns[ev.id] : starkInfo.qs[ev.id];
            VarPolMap polInfo = starkInfo.varPolMap[idPol];
            uint64_t dim = polInfo.dim;
            if (dim == 1)
            {
                sorted_evMap[size_eval + counters[1]] = i;
                ++counters[1];
            }
            else
            {
                sorted_evMap[3 * size_eval + counters[3]] = i;
                ++counters[3];
            }
        }
        else if (ev.type == EvMap::eType::q)
        {
            uint16_t idPol = (ev.type == EvMap::eType::cm) ? starkInfo.cm_2ns[ev.id] : starkInfo.qs[ev.id];
            VarPolMap polInfo = starkInfo.varPolMap[idPol];
            uint64_t dim = polInfo.dim;
            if (dim == 1)
            {
                sorted_evMap[2 * size_eval + counters[2]] = i;
                ++counters[2];
            }
            else
            {
                sorted_evMap[4 * size_eval + counters[4]] = i;
                ++counters[4];
            }
        }
        else
        {
            throw std::invalid_argument("Invalid ev type: " + ev.type);
        }
    }
    // join subsets 1 and 2 in 1
    int offset1 = size_eval + counters[1];
    int offset2 = 2 * size_eval;
    for (uint64_t i = 0; i < counters[2]; ++i)
    {
        sorted_evMap[offset1 + i] = sorted_evMap[offset2 + i];
        ++counters[1];
    }
    // join subsets 3 and 4 in 3
    offset1 = 3 * size_eval + counters[3];
    offset2 = 4 * size_eval;
    for (uint64_t i = 0; i < counters[4]; ++i)
    {
        sorted_evMap[offset1 + i] = sorted_evMap[offset2 + i];
        ++counters[3];
    }
    // Buffer for partial results of the matrix-vector product (columns distribution)
    int num_threads = omp_get_max_threads();
    Goldilocks::Element **evals_acc = (Goldilocks::Element **)malloc(num_threads * sizeof(Goldilocks::Element *));
    for (int i = 0; i < num_threads; ++i)
    {
        evals_acc[i] = (Goldilocks::Element *)malloc(size_eval * FIELD_EXTENSION * sizeof(Goldilocks::Element));
    }

#pragma omp parallel
    {
        int thread_idx = omp_get_thread_num();
        for (uint64_t i = 0; i < size_eval * FIELD_EXTENSION; ++i)
        {
            evals_acc[thread_idx][i] = Goldilocks::zero();
        }

#pragma omp for
        for (uint64_t k = 0; k < N; k++)
        {
            for (uint64_t i = 0; i < counters[0]; i++)
            {
                int indx = sorted_evMap[i];
                EvMap ev = starkInfo.evMap[indx];
                Polinomial tmp(1, FIELD_EXTENSION);
                Polinomial acc(1, FIELD_EXTENSION);

                Polinomial p(&((Goldilocks::Element *)pConstPols2ns->address())[ev.id], pConstPols2ns->degree(), 1, pConstPols2ns->numPols());

                Polinomial::mulElement(tmp, 0, ev.prime ? LpEv : LEv, k, p, k << extendBits);
                for (int j = 0; j < FIELD_EXTENSION; ++j)
                {
                    evals_acc[thread_idx][indx * FIELD_EXTENSION + j] = evals_acc[thread_idx][indx * FIELD_EXTENSION + j] + tmp[0][j];
                }
            }
            for (uint64_t i = 0; i < counters[1]; i++)
            {
                int indx = sorted_evMap[size_eval + i];
                EvMap ev = starkInfo.evMap[indx];
                Polinomial tmp(1, FIELD_EXTENSION);

                Polinomial p;
                p = (ev.type == EvMap::eType::cm) ? starkInfo.getPolinomial(mem, starkInfo.cm_2ns[ev.id]) : starkInfo.getPolinomial(mem, starkInfo.qs[ev.id]);

                Polinomial ::mulElement(tmp, 0, ev.prime ? LpEv : LEv, k, p, k << extendBits);
                for (int j = 0; j < FIELD_EXTENSION; ++j)
                {
                    evals_acc[thread_idx][indx * FIELD_EXTENSION + j] = evals_acc[thread_idx][indx * FIELD_EXTENSION + j] + tmp[0][j];
                }
            }
            for (uint64_t i = 0; i < counters[3]; i++)
            {
                int indx = sorted_evMap[3 * size_eval + i];
                EvMap ev = starkInfo.evMap[indx];
                Polinomial tmp(1, FIELD_EXTENSION);

                Polinomial p;
                p = (ev.type == EvMap::eType::cm) ? starkInfo.getPolinomial(mem, starkInfo.cm_2ns[ev.id]) : starkInfo.getPolinomial(mem, starkInfo.qs[ev.id]);

                Polinomial ::mulElement(tmp, 0, ev.prime ? LpEv : LEv, k, p, k << extendBits);
                for (int j = 0; j < FIELD_EXTENSION; ++j)
                {
                    evals_acc[thread_idx][indx * FIELD_EXTENSION + j] = evals_acc[thread_idx][indx * FIELD_EXTENSION + j] + tmp[0][j];
                }
            }
        }
#pragma omp for
        for (uint64_t i = 0; i < size_eval; ++i)
        {
            Goldilocks::Element sum0 = Goldilocks::zero();
            Goldilocks::Element sum1 = Goldilocks::zero();
            Goldilocks::Element sum2 = Goldilocks::zero();
            int offset = i * FIELD_EXTENSION;
            for (int k = 0; k < num_threads; ++k)
            {
                sum0 = sum0 + evals_acc[k][offset];
                sum1 = sum1 + evals_acc[k][offset + 1];
                sum2 = sum2 + evals_acc[k][offset + 2];
            }
            (evals[i])[0] = sum0;
            (evals[i])[1] = sum1;
            (evals[i])[2] = sum2;
        }
    }
    free(sorted_evMap);
    for (int i = 0; i < num_threads; ++i)
    {
        free(evals_acc[i]);
    }
    free(evals_acc);
}