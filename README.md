# Lite setup for testing contract calls
## Enable only zkExecutor
```
    Prover (available via GRPC service)
    |\
    | Executor (available via GRPC service)
    | |\
    | | Main State Machine
    | | Byte4 State Machine
    | | Binary State Machine
    | | Memory State Machine
    | | Mem Align State Machine
    | | Arithmetic State Machine
    | | Storage State Machine------\
    | |                             |--> Poseidon G State Machine
    | | Padding PG State Machine---/
    | | Padding KK SM -> Padding KK Bit -> Nine To One SM -> Keccak-f SM -> Norm Gate 9 SM
    |  \
    |   State DB (available via GRPC service)
    |   |\
    |   | SMT
    |    \
    |     Database
    |\
    | Stark
    |\
    | Circom
```

We would need to use the config flag runFileExecute in the config file.

## Use example benchmark such as uniswapv2
The input can be located in the performance folder (https://github.com/namnc/zkevm-prover-pg/tree/main/testvectors/performance), named uniswap_swaps_21.json (https://github.com/namnc/zkevm-prover-pg/blob/main/testvectors/performance/uniswap_swaps_21.json).
To run the benchmark invoke the prover within the testvectors folder (https://github.com/namnc/zkevm-prover-pg/tree/main/testvectors) using the modified config file (e.g. named config_runFile_Executor.json) with input to the json file:
```
../build/zkProver -c config_runFile_Executor.json ./performance/uniswap_swaps_21.json
```

## Understanding the benchmarking result
zkEVM can fit 2^23 actions (can be thought of as cpu cycles or rows of computation), and we would like to learn how complex is the contract call of our interest, i.e. how many actions are needed for the executed contract call. This gives us some ideas on optimizing contracts when using zkEVM.

## Generate test input for zkExecutor
Look into the zkevm-testvectors repository (https://github.com/namnc/zkevm-testvectors-pg), we can generate test input from contracts:
- Contracts in solidity should be put in the tools-calldata/evm/contracts folder: https://github.com/namnc/zkevm-testvectors-pg/tree/main/tools-calldata/evm/contracts
- Add or modify generate-test-vectors file --> example: gen-example.json. Take uniswapv2 contract calls for example, we can have a look at gen-uniswapv2.json (https://github.com/namnc/zkevm-testvectors-pg/blob/main/tools-calldata/evm/generate-test-vectors/gen-uniswapv2.json). We can invoke call by adding a tx (after deploying the contract):
```
      {
        "from": "0x4d5Cf5032B2a844602278b01199ED191A86c93ff",
        "to": "deploy",
        "nonce": "0",
        "value": "0",
        "contractName": "UniswapV2Factory",
        "params": [
          "0x617b3a3528F9cDd6630fd3301B9c8911F7Bf063D"
        ],
        "gasLimit": 10000000,
        "gasPrice": "1000000000",
        "chainId": 1000
      }
```
```
    {
        "from": "0x617b3a3528F9cDd6630fd3301B9c8911F7Bf063D",
        "to": "contract",
        "nonce": "1",
        "value": "0",
        "contractAddress": "0x85e844b762a271022b692cf99ce5c59ba0650ac8",
        "abiName": "ERC20ABI",
        "function": "mint",
        "params": [
          "0x4d5Cf5032B2a844602278b01199ED191A86c93ff",
          "10000000000000000000"
        ],
        "gasLimit": 100000,
        "gasPrice": "1000000000",
        "chainId": 1000
      }
```
The hard part (genesis states, account balances, generating roots, etc.) are already handled by the handy js files.
- Run npx mocha gen-test-vectors-evm.js --vectors gen-example.json --> output: test-vectors/test-vector-data/example.json
- Run npx mocha gen-inputs.js --vectors example --update --output --> output: test-vectors/inputs-executor/inputs/input_example_X.json (one input for each id in test-vector-data)

The input_example_X.json is the expected input to be used with the zkEVM prover.

## Additional logs
We can enable additional logs in definitions: https://github.com/namnc/zkevm-prover-pg/blob/namnc-lite-prover-only-zkexecutor/src/config/definitions.hpp.

# zkEVM Prover
zkEVM proof generator
## General info
The zkEVM Prover process can provide up to 3 RPC services:

### Prover service
- It calls the Prover component that executes the input data (a batch of EVM transactions), calculates the resulting state, and generates the proof of the calculation based on the PIL polynomials definition and their constrains.
- When called by the Prover service, the Executor component combines 14 state machines that process the input data to generate the evaluations of the committed polynomials, required to generate the proof.  Every state machine generates their computation evidence data, and the more complex calculus demonstrations are delegated to the next state machine.
- The Prover component calls the Stark component to generate a proof of the Executor state machines committed polynomials.
- The interface of this service is defined by the file zk-prover.proto.

### Aggregator client
- It connects to an Aggregator server
- It calls the Prover component that executes the input data (a batch of EVM transactions), calculates the resulting state, and generates the proof of the calculation based on the PIL polynomials definition and their constrains.
- When called by the Aggregator service, the Executor component combines 14 state machines that process the input data to generate the evaluations of the committed polynomials, required to generate the proof.  Every state machine generates their computation evidence data, and the more complex calculus demonstrations are delegated to the next state machine.
- The Prover component calls the Stark component to generate a proof of the Executor state machines committed polynomials.
- The interface of the server of this service is defined by the file aggregator.proto.

### Executor service
- It calls the Executor component that executes the input data (a batch of EVM transactions) and calculates the resulting state.  The proof is not generated.
- It provides a fast way to check if the proposed batch of transactions is properly built and it fits the amount of work that can be proven in one single batch.
- When called by the Executor service, the Executor component only uses the Main state machine, since the committed polynomials are not required as the proof will not be generated.
- The interface of this service is defined by the file executor.proto.

### StateDB service
- It provides an interface to access the state of the system (a Merkle tree) and the database where the state is stored.
- It is used by the executor and the prover, as the single source of state.  It can be used to get state details, e.g. account balances.
- The interface of this service is defined by the file statedb.proto.

## Setup

### Clone repository
```sh
$ git clone git@github.com:0xPolygonHermez/zkevm-prover.git
$ cd zkevm-prover
$ git submodule init
$ git submodule update
```

### Compile
The following packages must be installed.
```sh
$ sudo apt install build-essential libbenchmark-dev libomp-dev libgmp-dev nlohmann-json3-dev postgresql libpqxx-dev libpqxx-doc nasm libsecp256k1-dev grpc-proto libsodium-dev libprotobuf-dev libssl-dev cmake libgrpc++-dev protobuf-compiler protobuf-compiler-grpc uuid-dev
```
To download the files needed to run the prover, you have to execute the following command, which will be downloaded in the `testvector` folder
```sh
$ make download_dependencies
```

Run `make` to compile the project (this process can take up to ~3 hours)
```sh
$ make clean
$ make -j
```

To run the testvector:
```sh
$ cd testvectors
$ ../build/zkProver -c config_runFile.json 
```

### StateDB service database
To use persistence in the StateDB (Merkle-tree) service you must create the database objects needed by the service. To do this run the shell script: 
```sh
$ ./tools/statedb/create_db.sh <database> <user> <password>
```
For example:
```sh
$ ./tools/statedb/create_db.sh testdb statedb statedb
```

### Build & run docker
```sh
$ sudo docker build -t zkprover .
$ sudo docker run --rm --network host -ti -p 50051:50051 -p 50061:50061 -p 50071:50071 -v $PWD/testvectors:/usr/src/app zkprover input_executor.json
```

## Usage
To execute the Prover you need to provide a `config.json` file that contains the parameters that allow us to configure the different Prover options. By default, the Prover loads the `config.json`file located in the `testvectors`folder. The most relevant parameters are commented below with the default value for the provided `config.json` file:

| Parameter | Description |
| --------- | ----------- |
| runStateDBServer | Enables StateDB GRPC service, provides SMT (Sparse Merkle Tree) and Database access |
| runExecutorServer | Enables Executor GRPC service, provides a service to process transaction batches |
| runAggregatorClient | Enables Aggregator GRPC client, connects to the Aggregator and process its requests |
| aggregatorClientHost | IP address of the Aggregator server to which the Aggregator client must connect to |
| runProverServer | Enables Prover GRPC service |
| runFileProcessBatch | Processes a batch using as input a JSON file defined in the `"inputFile"` parameter |
| runFileGenProof | Generates a proof using as input a JSON file defined in the `"inputFile"` parameter |
| inputFile | Input JSON file with path relative to the `testvectors` folder |
| outputPath | Output path folder to store the result files, with path relative to the `testvectors` folder |
| databaseURL | Connection string for the PostgreSQL database used by the StateDB service. If the value is `"local"` then the service will not use a database and the data will be stored only in memory (no persistence). The PostgreSQL database connection string has the following format: `"postgresql://<user>:<password>@<ip>:<port>/<database>"`. For example: `"postgresql://statedb:statedb@127.0.0.1:5432/testdb"` |
| stateDBURL | Connection string for the StateDB service. If the value is `"local"` then the GRPC StateDB service will not be used and local StateDB client will be used instead. The StateDB service connection string has the following format: `"<ip>:<port>"`. For example: `"127.0.0.1:50061"` |
| saveRequestToFile | Saves service received requests to a text file |
| saveResponseToFile | Saves service returned responses to a text file |
| saveInputToFile | Saves service received input data to a JSON file |
| saveOutputToFile | Saves service returned output data to a JSON file |

To run a proof test you must perform the following steps:
- Edit the `config.json` file and set the parameter `"runFileGenProof"` to `"true"`. The rest of the parameters must be set to `"false"`. Also set the parameter `"databaseURL` to `"local"` if you don't want to use a postgreSQL database to run the test
- Indicate in the `"inputFile"` parameter the file with the input test data. You can find a test file `input_executor.json` in the `testvectors` folder
- Run the Prover from the `testvectors` folder using the command `$ ../build/zkProver`
- The result files of the proof will be stored in the folder specified in the `"outputPath"` config parameter

## License

### Copyright
Polygon `zkevm-prover` was developed by Polygon. While we plan to adopt an open source license, we haven’t selected one yet, so all rights are reserved for the time being. Please reach out to us if you have thoughts on licensing.  
  
### Disclaimer
This code has not yet been audited, and should not be used in any production systems.

