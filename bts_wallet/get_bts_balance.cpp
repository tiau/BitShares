#include <fc/crypto/ripemd160.hpp>
#include "chain_server.hpp" 
#include <bts/bitcoin_wallet.hpp>
#include "chain_connection.hpp"
#include "chain_messages.hpp"
#include <fc/reflect/reflect.hpp>
#include <fc/io/console.hpp>
#include <mail/message.hpp>
#include <mail/stcp_socket.hpp>
#include <bts/blockchain/blockchain_db.hpp>
#include <bts/db/level_map.hpp>
#include <fc/time.hpp>
#include <fc/network/tcp_socket.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/thread/thread.hpp>
#include <fc/thread/future.hpp>
#include <fc/io/raw.hpp>
#include <fc/io/json.hpp>
#include <fc/log/logger.hpp>

#include <boost/program_options.hpp>

#include <bts/electrum_wallet.hpp>
#include <bts/armory_wallet.hpp>
#include <bts/multibit_wallet.hpp>

#include <iostream>

#include <algorithm>
#include <unordered_map>
#include <map>

const std::string wallet_types[] = {"armory", "blockchain.info", "bitcoin", "electrum", "multibit"};

struct genesis_block_config
{
   genesis_block_config():supply(0),blockheight(0){}
   double                                            supply;
   uint64_t                                          blockheight;
   std::unordered_map<bts::pts_address,uint64_t >    balances;
};
FC_REFLECT( genesis_block_config, (supply)(balances) )

namespace po = boost::program_options;

std::string get_pass(std::string type, std::string path)
{
   std::cout << type << " wallet at " << path << " passphrase: ";
   fc::set_console_echo( false );
   std::string phrase;
   std::getline( std::cin, phrase );
   fc::set_console_echo( true );
   std::cout << "decrypting"<< std::endl;
   return phrase;
}

uint64_t do_wallet(std::string type, std::string path, std::string phrase, struct genesis_block_config& config, bool verbose)
{
   auto keys = (type == "bitcoin" ?  bts::import_bitcoin_wallet( fc::path( path ), phrase )
            :  (type == "electrum" ? bts::import_electrum_wallet( fc::path( path ), phrase )
            :  (type == "multibit" ? bts::import_multibit_wallet( fc::path( path ), phrase )
//            :  (type == "armory" ?   bts::import_armory_wallet( fc::path( path ), phrase )
//            :  std::vector<fc::ecc::private_key>()))));
            :  std::vector<fc::ecc::private_key>()))); // TODO uncomment above 2 lines and delete this line once armory support exists

   if( verbose )
      std::cout << "loaded "<<keys.size()<< " keys from "<<type<<" wallet at "<<path<<std::endl;

   uint64_t balance = 0;
   for( auto itr = keys.begin(); itr != keys.end(); ++itr )
   {
      {
       auto addr = bts::pts_address( itr->get_public_key(), false, 0 );
       auto bitr = config.balances.find(addr);
       if( bitr != config.balances.end() ) balance += bitr->second;
      }
      {
       auto addr = bts::pts_address( itr->get_public_key(), true, 0 );
       auto bitr = config.balances.find(addr);
       if( bitr != config.balances.end() ) balance += bitr->second;
      }
      {
       auto addr = bts::pts_address( itr->get_public_key(), false );
       auto bitr = config.balances.find(addr);
       if( bitr != config.balances.end() ) balance += bitr->second;
      }
      {
       auto addr = bts::pts_address( itr->get_public_key(), true );
       auto bitr = config.balances.find(addr);
       if( bitr != config.balances.end() ) balance += bitr->second;
      }
      if( verbose )
         std::cout<<"balance: "<< double(balance)/COIN <<std::endl;
   }

   return double(balance)/COIN;
}

int main( int argc, char** argv )
{
   bool verbose = false;
   po::options_description desc("Options");
   desc.add_options()
      ("help", "see help")
      ("armory,A", po::value<std::vector<std::string> >(), "armory wallet location")
      ("bcinfo,B", po::value<std::vector<std::string> >(), "blockchain.info location")
      ("bitcoin,b", po::value<std::vector<std::string> >(), "bitcoin wallet.dat location")
      ("electrum,E", po::value<std::vector<std::string> >(), "electrum wallet location")
      ("multibit,M", po::value<std::vector<std::string> >(), "multibit wallet location")
      ("genesis,G", po::value<std::string>(), "REQUIRED location of genesis.json")
      ("verbose,v", "be more verbose")
   ;

   po::variables_map vm;
   po::store(po::parse_command_line(argc, argv, desc), vm);
   po::notify(vm);    

   if( vm.count("help") || !vm.count("genesis") )
   {
      std::cout << desc << std::endl;
      return 1;
   }

   if( vm.count("verbose") )
   {
      std::cout << "being verbose"<< std::endl;
      verbose = true;
   }

   uint64_t total = 0;
   try {
      std::string genesis = vm["genesis"].as<std::string>();
      FC_ASSERT( fc::exists( genesis ) );
      auto config = fc::json::from_file( genesis ).as<genesis_block_config>();
      for( const std::string& type : wallet_types )
      {
         if( vm.count(type) )
         {
            auto paths = vm[type].as< std::vector<std::string> >();
            for( const std::string& path : paths )
            {
               std::string phrase = get_pass(type, path);
               try {
                  uint64_t value = do_wallet(type, path, phrase, config, verbose);
                  total += value;
                  if( verbose )
                     std::cout << type <<" wallet located at "<< path <<" has "<< value <<" BTS"<< std::endl;
               }
               catch( const fc::exception& e )
               {
                  std::cout << "problem with "<< type <<" wallet "<< path <<" ; (typo in password?)  here's some more info:"<< std::endl<<e.to_detail_string() <<std::endl;
               }
            }
         }
      }
      std::cout << "Total BTS found: "<< total <<std::endl;
   }
   catch ( const fc::exception& e )
   {
       std::cerr<<e.to_detail_string()<<std::endl;
       return -1;
   }
   return 0;
}
