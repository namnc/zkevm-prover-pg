#include "config.hpp"
#include "executor_service.hpp"
#include "input.hpp"
#include "proof.hpp"
#include "full_tracer.hpp"

#include <grpcpp/grpcpp.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

::grpc::Status ExecutorServiceImpl::ProcessBatch(::grpc::ServerContext* context, const ::executor::v1::ProcessBatchRequest* request, ::executor::v1::ProcessBatchResponse* response)
{
    TimerStart(EXECUTOR_PROCESS_BATCH);

#ifdef LOG_SERVICE
    cout << "ExecutorServiceImpl::ProcessBatch() got request:\n" << request->DebugString() << endl;
#endif

    // Create and init an instance of ProverRequest
    ProverRequest proverRequest(fr, config, prt_processBatch);

    // Save request to file
    if (config.saveRequestToFile)
    {
        string2File(request->DebugString(), proverRequest.filePrefix + "executor_request.txt");
    }

    // PUBLIC INPUTS

    string auxString;

    // Get oldStateRoot
    auxString = ba2string(request->old_state_root());
    if (auxString.size() > 64)
    {
        cerr << "Error: ExecutorServiceImpl::ProcessBatch() got oldStateRoot too long, size=" << auxString.size() << endl;
        TimerStopAndLog(EXECUTOR_PROCESS_BATCH);
        return Status::CANCELLED;
    }
    proverRequest.input.publicInputsExtended.publicInputs.oldStateRoot.set_str(auxString, 16);

    // Get oldAccInputHash
    auxString = ba2string(request->old_acc_input_hash());
    if (auxString.size() > 64)
    {
        cerr << "Error: ExecutorServiceImpl::ProcessBatch() got oldAccInputHash too long, size=" << auxString.size() << endl;
        TimerStopAndLog(EXECUTOR_PROCESS_BATCH);
        return Status::CANCELLED;
    }
    proverRequest.input.publicInputsExtended.publicInputs.oldAccInputHash.set_str(auxString, 16);

    // Get batchNum
    proverRequest.input.publicInputsExtended.publicInputs.oldBatchNum = request->old_batch_num();

    // Get chain ID
    proverRequest.input.publicInputsExtended.publicInputs.chainID = request->chain_id();
    if (proverRequest.input.publicInputsExtended.publicInputs.chainID == 0)
    {
        cerr << "Error: ExecutorServiceImpl::ProcessBatch() got chainID = 0" << endl;
        TimerStopAndLog(EXECUTOR_PROCESS_BATCH);
        return Status::CANCELLED;
    }

    // Get batchL2Data
    proverRequest.input.publicInputsExtended.publicInputs.batchL2Data = request->batch_l2_data();

    // Check the batchL2Data length
    if (proverRequest.input.publicInputsExtended.publicInputs.batchL2Data.size() > MAX_BATCH_L2_DATA_SIZE)
    {
        cerr << "Error: ExecutorServiceImpl::ProcessBatch() found batchL2Data.size()=" << proverRequest.input.publicInputsExtended.publicInputs.batchL2Data.size() << " > MAX_BATCH_L2_DATA_SIZE=" << MAX_BATCH_L2_DATA_SIZE << endl;
        TimerStopAndLog(EXECUTOR_PROCESS_BATCH);
        return Status::CANCELLED;
    }

    // Get globalExitRoot
    if (request->global_exit_root().size() > 32)
    {
        cerr << "Error: ExecutorServiceImpl::ProcessBatch() got globalExitRoot too long, size=" << request->global_exit_root().size() << endl;
        TimerStopAndLog(EXECUTOR_PROCESS_BATCH);
        return Status::CANCELLED;
    }
    ba2scalar(proverRequest.input.publicInputsExtended.publicInputs.globalExitRoot, request->global_exit_root());

    // Get timestamp
    proverRequest.input.publicInputsExtended.publicInputs.timestamp = request->eth_timestamp();

    // Get sequencerAddr
    auxString = Remove0xIfPresent(request->coinbase());
    if (auxString.size() > 40)
    {
        cerr << "Error: ExecutorServiceImpl::ProcessBatch() got sequencer address too long, size=" << auxString.size() << endl;
        TimerStopAndLog(EXECUTOR_PROCESS_BATCH);
        return Status::CANCELLED;
    }
    proverRequest.input.publicInputsExtended.publicInputs.sequencerAddr.set_str(auxString, 16);

    // ROOT

    // Get from
    proverRequest.input.from = Add0xIfMissing(request->from());
    if (proverRequest.input.from.size() > (2 + 40))
    {
        cerr << "Error: ExecutorServiceImpl::ProcessBatch() got from too long, size=" << proverRequest.input.from.size() << endl;
        TimerStopAndLog(EXECUTOR_PROCESS_BATCH);
        return Status::CANCELLED;
    }

    // Flags
    proverRequest.input.bUpdateMerkleTree = request->update_merkle_tree();
    proverRequest.input.txHashToGenerateExecuteTrace = "0x" + ba2string(request->tx_hash_to_generate_execute_trace());
    proverRequest.input.txHashToGenerateCallTrace = "0x" + ba2string(request->tx_hash_to_generate_call_trace());

    // Default values
    proverRequest.input.publicInputsExtended.newStateRoot = "0x0";
    proverRequest.input.publicInputsExtended.newAccInputHash = "0x0";
    proverRequest.input.publicInputsExtended.newLocalExitRoot = "0x0";
    proverRequest.input.publicInputsExtended.newBatchNum = 0;

    // Parse db map
    google::protobuf::Map<std::__cxx11::basic_string<char>, std::__cxx11::basic_string<char> > db;
    db = request->db();
    google::protobuf::Map<std::__cxx11::basic_string<char>, std::__cxx11::basic_string<char> >::iterator it;
    for (it=db.begin(); it!=db.end(); it++)
    {
        if (it->first.size() > (64))
        {
            cerr << "Error: ExecutorServiceImpl::ProcessBatch() got db key too long, size=" << it->first.size() << endl;
            TimerStopAndLog(EXECUTOR_PROCESS_BATCH);
            return Status::CANCELLED;
        }
        vector<Goldilocks::Element> dbValue;
        string concatenatedValues = it->second;
        if (concatenatedValues.size()%16!=0)
        {
            cerr << "Error: ExecutorServiceImpl::ProcessBatch() found invalid db value size: " << concatenatedValues.size() << endl;
            TimerStopAndLog(EXECUTOR_PROCESS_BATCH);
            return Status::CANCELLED;
        }
        for (uint64_t i=0; i<concatenatedValues.size(); i+=16)
        {
            Goldilocks::Element fe;
            string2fe(fr, concatenatedValues.substr(i, 16), fe);
            dbValue.push_back(fe);
        }
        Goldilocks::Element fe;
        string2fe(fr, it->first, fe);
        proverRequest.input.db[it->first] = dbValue;
#ifdef LOG_SERVICE_EXECUTOR_INPUT
        //cout << "input.db[" << it->first << "]: " << proverRequest.input.db[it->first] << endl;
#endif
    }

    // Parse contracts data
    google::protobuf::Map<std::__cxx11::basic_string<char>, std::__cxx11::basic_string<char> > contractsBytecode;
    contractsBytecode = request->contracts_bytecode();
    google::protobuf::Map<std::__cxx11::basic_string<char>, std::__cxx11::basic_string<char> >::iterator itp;
    for (itp=contractsBytecode.begin(); itp!=contractsBytecode.end(); itp++)
    {
        vector<uint8_t> dbValue;
        string contractValue = string2ba(itp->second);
        for (uint64_t i=0; i<contractValue.size(); i++)
        {
            dbValue.push_back(contractValue.at(i));
        }
        proverRequest.input.contractsBytecode[itp->first] = dbValue;
#ifdef LOG_SERVICE_EXECUTOR_INPUT
        //cout << "proverRequest.input.contractsBytecode[" << itp->first << "]: " << itp->second << endl;
#endif
    }

    // Get no counters flag
    proverRequest.input.bNoCounters = request->no_counters();

#ifdef LOG_SERVICE_EXECUTOR_INPUT
    cout << "ExecutorServiceImpl::ProcessBatch() got sequencerAddr=" << proverRequest.input.publicInputsExtended.publicInputs.sequencerAddr.get_str(16)
        << " batchL2DataLength=" << request->batch_l2_data().size()
        << " batchL2Data=0x" << ba2string(proverRequest.input.publicInputsExtended.publicInputs.batchL2Data.substr(0, 10)) << "..." << ba2string(proverRequest.input.publicInputsExtended.publicInputs.batchL2Data.substr(zkmax(int64_t(0),int64_t(proverRequest.input.publicInputsExtended.publicInputs.batchL2Data.size())-10), proverRequest.input.publicInputsExtended.publicInputs.batchL2Data.size()))
        << " oldStateRoot=" << proverRequest.input.publicInputsExtended.publicInputs.oldStateRoot.get_str(16)
        << " oldAccInputHash=" << proverRequest.input.publicInputsExtended.publicInputs.oldAccInputHash.get_str(16)
        << " oldBatchNum=" << proverRequest.input.publicInputsExtended.publicInputs.oldBatchNum
        << " chainId=" << proverRequest.input.publicInputsExtended.publicInputs.chainID
        << " globalExitRoot=" << proverRequest.input.publicInputsExtended.publicInputs.globalExitRoot.get_str(16)
        << " timestamp=" << proverRequest.input.publicInputsExtended.publicInputs.timestamp

        << " from=" << proverRequest.input.from
        << " bUpdateMerkleTree=" << proverRequest.input.bUpdateMerkleTree
        << " bNoCounters=" << proverRequest.input.bNoCounters
        << " txHashToGenerateExecuteTrace=" << proverRequest.input.txHashToGenerateExecuteTrace
        << " txHashToGenerateCallTrace=" << proverRequest.input.txHashToGenerateCallTrace
        << endl;
#endif

    if (config.useProcessBatchCache)
    {
        bool bFoundInCache = processBatchCache.Read(proverRequest);
        if (!bFoundInCache)
        {
            prover.processBatch(&proverRequest);
            if (proverRequest.result == ZKR_SUCCESS)
            {
                processBatchCache.Write(proverRequest);
            }
        }
    }
    else
    {
        prover.processBatch(&proverRequest);
    }

    if (proverRequest.result != ZKR_SUCCESS)
    {
        cerr << "Error: ExecutorServiceImpl::ProcessBatch() detected proverRequest.result=" << proverRequest.result << "=" << zkresult2string(proverRequest.result) << endl;
        response->set_error(zkresult2error(proverRequest.result));
    }
    else
    {
        response->set_error(string2error(proverRequest.fullTracer.finalTrace.error));
    }
    
    response->set_cumulative_gas_used(proverRequest.fullTracer.finalTrace.cumulative_gas_used);
    response->set_cnt_keccak_hashes(proverRequest.counters.keccakF);
    response->set_cnt_poseidon_hashes(proverRequest.counters.poseidonG);
    response->set_cnt_poseidon_paddings(proverRequest.counters.paddingPG);
    response->set_cnt_mem_aligns(proverRequest.counters.memAlign);
    response->set_cnt_arithmetics(proverRequest.counters.arith);
    response->set_cnt_binaries(proverRequest.counters.binary);
    response->set_cnt_steps(proverRequest.counters.steps);
    response->set_new_state_root(string2ba(proverRequest.fullTracer.finalTrace.new_state_root));
    response->set_new_acc_input_hash(string2ba(proverRequest.fullTracer.finalTrace.new_acc_input_hash));
    response->set_new_local_exit_root(string2ba(proverRequest.fullTracer.finalTrace.new_local_exit_root));
    vector<Response> &responses(proverRequest.fullTracer.finalTrace.responses);
    for (uint64_t tx=0; tx<responses.size(); tx++)
    {
        executor::v1::ProcessTransactionResponse * pProcessTransactionResponse = response->add_responses();
        pProcessTransactionResponse->set_tx_hash(string2ba(responses[tx].tx_hash));
        pProcessTransactionResponse->set_rlp_tx(responses[tx].rlp_tx);
        pProcessTransactionResponse->set_type(responses[tx].type); // Type indicates legacy transaction; it will be always 0 (legacy) in the executor
        pProcessTransactionResponse->set_return_value(string2ba(responses[tx].return_value)); // Returned data from the runtime (function result or data supplied with revert opcode)
        pProcessTransactionResponse->set_gas_left(responses[tx].gas_left); // Total gas left as result of execution
        pProcessTransactionResponse->set_gas_used(responses[tx].gas_used); // Total gas used as result of execution or gas estimation
        pProcessTransactionResponse->set_gas_refunded(responses[tx].gas_refunded); // Total gas refunded as result of execution
        pProcessTransactionResponse->set_error(string2error(responses[tx].error)); // Any error encountered during the execution
        pProcessTransactionResponse->set_create_address(responses[tx].create_address); // New SC Address in case of SC creation
        pProcessTransactionResponse->set_state_root(string2ba(responses[tx].state_root));
        for (uint64_t log=0; log<responses[tx].logs.size(); log++)
        {
            executor::v1::Log * pLog = pProcessTransactionResponse->add_logs();
            pLog->set_address(responses[tx].logs[log].address); // Address of the contract that generated the event
            for (uint64_t topic=0; topic<responses[tx].logs[log].topics.size(); topic++)
            {
                std::string * pTopic = pLog->add_topics();
                *pTopic = string2ba(responses[tx].logs[log].topics[topic]); // List of topics provided by the contract
            }
            string dataConcatenated;
            for (uint64_t data=0; data<responses[tx].logs[log].data.size(); data++)
                dataConcatenated += responses[tx].logs[log].data[data];
            pLog->set_data(string2ba(dataConcatenated)); // Supplied by the contract, usually ABI-encoded
            pLog->set_batch_number(responses[tx].logs[log].batch_number); // Batch in which the transaction was included
            pLog->set_tx_hash(string2ba(responses[tx].logs[log].tx_hash)); // Hash of the transaction
            pLog->set_tx_index(responses[tx].logs[log].tx_index); // Index of the transaction in the block
            pLog->set_batch_hash(string2ba(responses[tx].logs[log].batch_hash)); // Hash of the batch in which the transaction was included
            pLog->set_index(responses[tx].logs[log].index); // Index of the log in the block
        }
        if (proverRequest.input.txHashToGenerateExecuteTrace == responses[tx].tx_hash)
        {
            for (uint64_t step=0; step<responses[tx].call_trace.steps.size(); step++)
            {
                executor::v1::ExecutionTraceStep * pExecutionTraceStep = pProcessTransactionResponse->add_execution_trace();
                pExecutionTraceStep->set_pc(responses[tx].call_trace.steps[step].pc); // Program Counter
                pExecutionTraceStep->set_op(responses[tx].call_trace.steps[step].opcode); // OpCode
                pExecutionTraceStep->set_remaining_gas(responses[tx].call_trace.steps[step].gas);
                pExecutionTraceStep->set_gas_cost(responses[tx].call_trace.steps[step].gas_cost); // Gas cost of the operation
                pExecutionTraceStep->set_memory(string2ba(responses[tx].call_trace.steps[step].memory)); // Content of memory
                pExecutionTraceStep->set_memory_size(responses[tx].call_trace.steps[step].memory_size);
                for (uint64_t stack=0; stack<responses[tx].call_trace.steps[step].stack.size() ; stack++)
                    pExecutionTraceStep->add_stack(PrependZeros(responses[tx].call_trace.steps[step].stack[stack].get_str(16), 64)); // Content of the stack
                string dataConcatenated;
                for (uint64_t data=0; data<responses[tx].call_trace.steps[step].return_data.size(); data++)
                    dataConcatenated += responses[tx].call_trace.steps[step].return_data[data];
                pExecutionTraceStep->set_return_data(string2ba(dataConcatenated));
                google::protobuf::Map<std::string, std::string>  * pStorage = pExecutionTraceStep->mutable_storage();
                unordered_map<string,string>::iterator it;
                for (it=responses[tx].call_trace.steps[step].storage.begin(); it!=responses[tx].call_trace.steps[step].storage.end(); it++)
                    (*pStorage)[it->first] = it->second; // Content of the storage
                pExecutionTraceStep->set_depth(responses[tx].call_trace.steps[step].depth); // Call depth
                pExecutionTraceStep->set_gas_refund(responses[tx].call_trace.steps[step].gas_refund);
                pExecutionTraceStep->set_error(string2error(responses[tx].call_trace.steps[step].error));
            }
        }
        if (proverRequest.input.txHashToGenerateCallTrace == responses[tx].tx_hash)
        {
            executor::v1::CallTrace * pCallTrace = new executor::v1::CallTrace();
            executor::v1::TransactionContext * pTransactionContext = pCallTrace->mutable_context();
            pTransactionContext->set_type(responses[tx].call_trace.context.type); // "CALL" or "CREATE"
            pTransactionContext->set_from(responses[tx].call_trace.context.from); // Sender of the transaction
            pTransactionContext->set_to(responses[tx].call_trace.context.to); // Target of the transaction
            pTransactionContext->set_data(string2ba(responses[tx].call_trace.context.data)); // Input data of the transaction
            pTransactionContext->set_gas(responses[tx].call_trace.context.gas);
            pTransactionContext->set_gas_price(Add0xIfMissing(responses[tx].call_trace.context.gas_price.get_str(16)));
            pTransactionContext->set_value(Add0xIfMissing(responses[tx].call_trace.context.value.get_str(16)));
            pTransactionContext->set_batch(string2ba(responses[tx].call_trace.context.batch)); // Hash of the batch in which the transaction was included
            pTransactionContext->set_output(string2ba(responses[tx].call_trace.context.output)); // Returned data from the runtime (function result or data supplied with revert opcode)
            pTransactionContext->set_gas_used(responses[tx].call_trace.context.gas_used); // Total gas used as result of execution
            pTransactionContext->set_execution_time(responses[tx].call_trace.context.execution_time);
            pTransactionContext->set_old_state_root(string2ba(responses[tx].call_trace.context.old_state_root)); // Starting state root
            for (uint64_t step=0; step<responses[tx].call_trace.steps.size(); step++)
            {
                executor::v1::TransactionStep * pTransactionStep = pCallTrace->add_steps();
                pTransactionStep->set_state_root(string2ba(responses[tx].call_trace.steps[step].state_root));
                pTransactionStep->set_depth(responses[tx].call_trace.steps[step].depth); // Call depth
                pTransactionStep->set_pc(responses[tx].call_trace.steps[step].pc); // Program counter
                pTransactionStep->set_gas(responses[tx].call_trace.steps[step].gas); // Remaining gas
                pTransactionStep->set_gas_cost(responses[tx].call_trace.steps[step].gas_cost); // Gas cost of the operation
                pTransactionStep->set_gas_refund(responses[tx].call_trace.steps[step].gas_refund); // Gas refunded during the operation
                pTransactionStep->set_op(responses[tx].call_trace.steps[step].op); // Opcode
                for (uint64_t stack=0; stack<responses[tx].call_trace.steps[step].stack.size() ; stack++)
                    pTransactionStep->add_stack(PrependZeros(responses[tx].call_trace.steps[step].stack[stack].get_str(16), 64)); // Content of the stack
                pTransactionStep->set_memory(string2ba(responses[tx].call_trace.steps[step].memory)); // Content of the memory
                string dataConcatenated;
                for (uint64_t data=0; data<responses[tx].call_trace.steps[step].return_data.size(); data++)
                    dataConcatenated += responses[tx].call_trace.steps[step].return_data[data];
                pTransactionStep->set_return_data(string2ba(dataConcatenated));
                executor::v1::Contract * pContract = pTransactionStep->mutable_contract(); // Contract information
                pContract->set_address(responses[tx].call_trace.steps[step].contract.address);
                pContract->set_caller(responses[tx].call_trace.steps[step].contract.caller);
                pContract->set_value(Add0xIfMissing(responses[tx].call_trace.steps[step].contract.value.get_str(16)));
                pContract->set_data(string2ba(responses[tx].call_trace.steps[step].contract.data));
                pContract->set_gas(responses[tx].call_trace.steps[step].contract.gas);
                pTransactionStep->set_error(string2error(responses[tx].call_trace.steps[step].error));
            }
            pProcessTransactionResponse->set_allocated_call_trace(pCallTrace);
        }
    }

#ifdef LOG_SERVICE_EXECUTOR_OUTPUT
    cout << "ExecutorServiceImpl::ProcessBatch() returns"
         << " new_state_root=" << proverRequest.fullTracer.finalTrace.new_state_root
         << " new_acc_input_hash=" << proverRequest.fullTracer.finalTrace.new_acc_input_hash
         << " new_local_exit_root=" << proverRequest.fullTracer.finalTrace.new_local_exit_root
         //<< " new_batch_num=" << proverRequest.fullTracer.finalTrace.new_batch_num
         << " steps=" << proverRequest.counters.steps
         << " gasUsed=" << proverRequest.fullTracer.finalTrace.cumulative_gas_used
         << " counters.keccakF=" << proverRequest.counters.keccakF
         << " counters.poseidonG=" << proverRequest.counters.poseidonG
         << " counters.paddingPG=" << proverRequest.counters.paddingPG
         << " counters.memAlign=" << proverRequest.counters.memAlign
         << " counters.arith=" << proverRequest.counters.arith
         << " counters.binary=" << proverRequest.counters.binary
         << " nTxs=" << responses.size();
        for (uint64_t tx=0; tx<responses.size(); tx++)
        {
            cout << " tx[" << tx << "].hash=" << responses[tx].tx_hash
                 << " gasUsed=" << responses[tx].gas_used
                 << " gasLeft=" << responses[tx].gas_left
                 << " gasUsed+gasLeft=" << (responses[tx].gas_used + responses[tx].gas_left)
                 << " gasRefunded=" << responses[tx].gas_refunded
                 << " error=" << responses[tx].error;
        }
        cout << endl;
#endif

    if (config.logExecutorServerResponses)
    {
        cout << "ExecutorServiceImpl::ProcessBatch() returns:\n" << response->DebugString() << endl;
    }

    TimerStopAndLog(EXECUTOR_PROCESS_BATCH);

    if (config.saveResponseToFile)
    {
        string2File(response->DebugString(), proverRequest.filePrefix + "executor_response.txt");
    }

    if (config.opcodeTracer)
    {
        map<uint8_t, vector<Opcode>> opcodeMap;
        cout << "Received " << proverRequest.fullTracer.info.size() << " opcodes:" << endl;
        for (uint64_t i=0; i<proverRequest.fullTracer.info.size(); i++)
        {
            if (opcodeMap.find(proverRequest.fullTracer.info[i].op) == opcodeMap.end())
            {
                vector<Opcode> aux;
                opcodeMap[proverRequest.fullTracer.info[i].op] = aux;
            }
            opcodeMap[proverRequest.fullTracer.info[i].op].push_back(proverRequest.fullTracer.info[i]);
        }
        map<uint8_t, vector<Opcode>>::iterator opcodeMapIt;
        for (opcodeMapIt = opcodeMap.begin(); opcodeMapIt != opcodeMap.end(); opcodeMapIt++)
        {
            cout << "    0x" << byte2string(opcodeMapIt->first) << "=" << opcodeMapIt->second[0].opcode << " called " << opcodeMapIt->second.size() << " times";

            uint64_t opcodeTotalGas = 0;
            cout << " gas=";
            for (uint64_t i=0; i<opcodeMapIt->second.size(); i++)
            {
                cout << opcodeMapIt->second[i].gas_cost << ",";
                opcodeTotalGas += opcodeMapIt->second[i].gas_cost;
            }

            uint64_t opcodeTotalDuration = 0;
            cout << " duration=";
            for (uint64_t i=0; i<opcodeMapIt->second.size(); i++)
            {
                cout << opcodeMapIt->second[i].duration << ",";
                opcodeTotalDuration += opcodeMapIt->second[i].duration;
            }

            cout << " TP=" << (double(opcodeTotalGas)*1000000)/double(opcodeTotalDuration) << "gas/s" << endl;
        }
    }

    // Calculate the throughput, for this ProcessBatch call, and for all calls
#ifdef LOG_TIME
    lock();
    counter++;
    uint64_t execGas = response->cumulative_gas_used();
    totalGas += execGas;
    uint64_t execBytes = request->batch_l2_data().size();
    totalBytes += execBytes;
    double execTime = double(TimeDiff(EXECUTOR_PROCESS_BATCH_start, EXECUTOR_PROCESS_BATCH_stop))/1000000;
    totalTime += execTime;
    struct timeval now;
    gettimeofday(&now, NULL);
    double timeSinceLastTotal = double(TimeDiff(lastTotalTime, now))/1000000;
    if (timeSinceLastTotal >= 1.0)
    {
        totalTPG = double(totalGas - lastTotalGas)/timeSinceLastTotal;
        totalTPB = double(totalBytes - lastTotalBytes)/timeSinceLastTotal;
        lastTotalGas = totalGas;
        lastTotalBytes = totalBytes;
        lastTotalTime = now;
    }
    cout << "ExecutorServiceImpl::ProcessBatch() done counter=" << counter << " B=" << execBytes <<  " gas=" << execGas << " time=" << execTime << " TP=" << double(execBytes)/execTime << "B/s=" << double(execGas)/execTime << "gas/s=" << double(execGas)/double(execBytes) << "gas/B totalTP=" << totalTPB << "B/s=" << totalTPG << "gas/s=" << totalTPG/totalTPB << "gas/B" << endl;
    unlock();
#endif

    return Status::OK;
}

::executor::v1::Error ExecutorServiceImpl::string2error (string &errorString)
{
    if (errorString == "OOG") return ::executor::v1::ERROR_OUT_OF_GAS;
    if (errorString == "revert") return ::executor::v1::ERROR_EXECUTION_REVERTED;
    if (errorString == "overflow") return ::executor::v1::ERROR_STACK_OVERFLOW;
    if (errorString == "underflow") return ::executor::v1::ERROR_STACK_UNDERFLOW;
    if (errorString == "OOCS") return ::executor::v1::ERROR_OUT_OF_COUNTERS_STEP;
    if (errorString == "OOCK") return ::executor::v1::ERROR_OUT_OF_COUNTERS_KECCAK;
    if (errorString == "OOCB") return ::executor::v1::ERROR_OUT_OF_COUNTERS_BINARY;
    if (errorString == "OOCM") return ::executor::v1::ERROR_OUT_OF_COUNTERS_MEM;
    if (errorString == "OOCA") return ::executor::v1::ERROR_OUT_OF_COUNTERS_ARITH;
    if (errorString == "OOCPA") return ::executor::v1::ERROR_OUT_OF_COUNTERS_PADDING;
    if (errorString == "OOCPO") return ::executor::v1::ERROR_OUT_OF_COUNTERS_POSEIDON;
    if (errorString == "intrinsic_invalid_signature") return ::executor::v1::ERROR_INTRINSIC_INVALID_SIGNATURE;
    if (errorString == "intrinsic_invalid_chain_id") return ::executor::v1::ERROR_INTRINSIC_INVALID_CHAIN_ID;
    if (errorString == "intrinsic_invalid_nonce") return ::executor::v1::ERROR_INTRINSIC_INVALID_NONCE;
    if (errorString == "intrinsic_invalid_gas_limit") return ::executor::v1::ERROR_INTRINSIC_INVALID_GAS_LIMIT;
    if (errorString == "intrinsic_invalid_gas_overflow") return ::executor::v1::ERROR_INTRINSIC_TX_GAS_OVERFLOW;
    if (errorString == "intrinsic_invalid_balance") return ::executor::v1::ERROR_INTRINSIC_INVALID_BALANCE;
    if (errorString == "intrinsic_invalid_batch_gas_limit") return ::executor::v1::ERROR_INTRINSIC_INVALID_BATCH_GAS_LIMIT;
    if (errorString == "intrinsic_invalid_sender_code") return ::executor::v1::ERROR_INTRINSIC_INVALID_SENDER_CODE;
    if (errorString == "invalidJump") return ::executor::v1::ERROR_INVALID_JUMP;
    if (errorString == "invalidOpcode") return ::executor::v1::ERROR_INVALID_OPCODE;
    if (errorString == "invalidAddressCollision") return ::executor::v1::ERROR_CONTRACT_ADDRESS_COLLISION;
    if (errorString == "invalidStaticTx") return ::executor::v1::ERROR_INVALID_STATIC;
    if (errorString == "invalidCodeSize") return ::executor::v1::ERROR_MAX_CODE_SIZE_EXCEEDED;
    if (errorString == "invalidCodeStartsEF") return ::executor::v1::ERROR_INVALID_BYTECODE_STARTS_EF;
    if (errorString == "") return ::executor::v1::ERROR_NO_ERROR;
    cerr << "Error: ExecutorServiceImpl::string2error() found invalid error string=" << errorString << endl;
    exitProcess();
    return ::executor::v1::ERROR_UNSPECIFIED;
}

::executor::v1::Error ExecutorServiceImpl::zkresult2error (zkresult &result)
{
    if (result == ZKR_SUCCESS) return ::executor::v1::ERROR_NO_ERROR;
    if (result == ZKR_SM_MAIN_OOC_ARITH) return ::executor::v1::ERROR_OUT_OF_COUNTERS_ARITH;
    if (result == ZKR_SM_MAIN_OOC_BINARY) return ::executor::v1::ERROR_OUT_OF_COUNTERS_BINARY;
    if (result == ZKR_SM_MAIN_OOC_KECCAK_F) return ::executor::v1::ERROR_OUT_OF_COUNTERS_KECCAK;
    if (result == ZKR_SM_MAIN_OOC_MEM_ALIGN) return ::executor::v1::ERROR_OUT_OF_COUNTERS_MEM;
    if (result == ZKR_SM_MAIN_OOC_PADDING_PG) return ::executor::v1::ERROR_OUT_OF_COUNTERS_PADDING;
    if (result == ZKR_SM_MAIN_OOC_POSEIDON_G) return ::executor::v1::ERROR_OUT_OF_COUNTERS_POSEIDON;
    if (result == ZKR_SM_MAIN_BATCH_L2_DATA_TOO_BIG) return ::executor::v1::ERROR_BATCH_DATA_TOO_BIG;
    return ::executor::v1::ERROR_UNSPECIFIED;
}