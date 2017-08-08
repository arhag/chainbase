#define BOOST_TEST_MODULE chainbase test

#include <boost/test/unit_test.hpp>
#include <chainbase/chainbase.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/composite_key.hpp>

#include <iostream>

using namespace chainbase;
using namespace boost::multi_index;

//BOOST_TEST_SUITE( serialization_tests, clean_database_fixture )

struct book : public chainbase::object<0, book> {
    CHAINBASE_DEFAULT_CONSTRUCTOR(book)

    id_type id;
    int a = 0;
    int b = 1;
};

typedef multi_index_container<
  book,
  indexed_by<
     ordered_unique< member<book,book::id_type,&book::id> >,
     ordered_non_unique< BOOST_MULTI_INDEX_MEMBER(book,int,a) >,
     ordered_non_unique< BOOST_MULTI_INDEX_MEMBER(book,int,b) >
  >,
  chainbase::allocator<book>
> book_index;

CHAINBASE_SET_INDEX_TYPE( book, book_index )

struct author : public chainbase::object<1, author> {
    CHAINBASE_DEFAULT_CONSTRUCTOR(author, (name))
    
    id_type id;
    shared_string name;
    int num_books = 0;
};

struct by_name;
struct by_num_books;

using author_index = shared_multi_index_container<
   author,
   indexed_by<
      ordered_unique< member<author, author::id_type, &author::id> >,
      ordered_non_unique< tag<by_name>, member<author, shared_string, &author::name> >,
      ordered_unique< tag<by_num_books>, 
         composite_key<author, 
            member<author, int, &author::num_books>,
            member<author, shared_string, &author::name>,
            member<author, author::id_type, &author::id>
         >,
         composite_key_compare< std::greater<int>, std::less<shared_string>, std::less<author::id_type> >
      >
   >
>;

CHAINBASE_SET_INDEX_TYPE( author, author_index )

BOOST_AUTO_TEST_CASE( open_and_create ) {
   boost::filesystem::path temp = boost::filesystem::unique_path();
   try {
      std::cerr << temp.native() << " \n";

      chainbase::database db(temp, database::read_write, 1024*1024*8);
      chainbase::database db2(temp); /// open an already created db
      BOOST_CHECK_THROW( db2.add_index< book_index >(), std::runtime_error ); /// index does not exist in read only database

      db.add_index< book_index >();
      BOOST_CHECK_THROW( db.add_index<book_index>(), std::logic_error ); /// cannot add same index twice


      db2.add_index< book_index >(); /// index should exist now


      BOOST_TEST_MESSAGE( "Creating book" );
      const auto& new_book = db.create<book>( []( book& b ) {
          b.a = 3;
          b.b = 4;
      } );
      const auto& copy_new_book = db2.get( book::id_type(0) );
      BOOST_REQUIRE( &new_book != &copy_new_book ); ///< these are mapped to different address ranges

      BOOST_REQUIRE_EQUAL( new_book.a, copy_new_book.a );
      BOOST_REQUIRE_EQUAL( new_book.b, copy_new_book.b );

      db.modify( new_book, [&]( book& b ) {
          b.a = 5;
          b.b = 6;
      });
      BOOST_REQUIRE_EQUAL( new_book.a, 5 );
      BOOST_REQUIRE_EQUAL( new_book.b, 6 );

      BOOST_REQUIRE_EQUAL( new_book.a, copy_new_book.a );
      BOOST_REQUIRE_EQUAL( new_book.b, copy_new_book.b );

      {
          auto session = db.start_undo_session(true);
          db.modify( new_book, [&]( book& b ) {
              b.a = 7;
              b.b = 8;
          });

         BOOST_REQUIRE_EQUAL( new_book.a, 7 );
         BOOST_REQUIRE_EQUAL( new_book.b, 8 );
      }
      BOOST_REQUIRE_EQUAL( new_book.a, 5 );
      BOOST_REQUIRE_EQUAL( new_book.b, 6 );

      {
          auto session = db.start_undo_session(true);
          const auto& book2 = db.create<book>( [&]( book& b ) {
              b.a = 9;
              b.b = 10;
          });

         BOOST_REQUIRE_EQUAL( new_book.a, 5 );
         BOOST_REQUIRE_EQUAL( new_book.b, 6 );
         BOOST_REQUIRE_EQUAL( book2.a, 9 );
         BOOST_REQUIRE_EQUAL( book2.b, 10 );
      }
      BOOST_CHECK_THROW( db2.get( book::id_type(1) ), std::out_of_range );
      BOOST_REQUIRE_EQUAL( new_book.a, 5 );
      BOOST_REQUIRE_EQUAL( new_book.b, 6 );


      {
          auto session = db.start_undo_session(true);
          db.modify( new_book, [&]( book& b ) {
              b.a = 7;
              b.b = 8;
          });

         BOOST_REQUIRE_EQUAL( new_book.a, 7 );
         BOOST_REQUIRE_EQUAL( new_book.b, 8 );
         session.push();
      }
      BOOST_REQUIRE_EQUAL( new_book.a, 7 );
      BOOST_REQUIRE_EQUAL( new_book.b, 8 );
      db.undo();
      BOOST_REQUIRE_EQUAL( new_book.a, 5 );
      BOOST_REQUIRE_EQUAL( new_book.b, 6 );

      BOOST_REQUIRE_EQUAL( new_book.a, copy_new_book.a );
      BOOST_REQUIRE_EQUAL( new_book.b, copy_new_book.b );
   } catch ( ... ) {
      bfs::remove_all( temp );
      throw;
   }
   bfs::remove_all( temp );
}

BOOST_AUTO_TEST_CASE( check_revision ) {
   boost::filesystem::path temp = boost::filesystem::unique_path();
   try {
      std::cerr << temp.native() << " \n";

      chainbase::database db(temp, database::read_write, 1024*1024*8);
      
      BOOST_REQUIRE_EQUAL( db.revision(), -1 ); /// No indices currently exist at this point
      
      db.add_index< book_index >();

      BOOST_REQUIRE_EQUAL( db.revision(), 0 ); /// After adding an index, the revision should now be the default revision of 0

      db.set_revision( 42 ); /// Set revision to arbitrary number

      BOOST_REQUIRE_EQUAL( db.revision(), 42 ); /// Make sure set revision worked

      BOOST_TEST_MESSAGE( "Creating book" );
      const auto& new_book = db.create<book>( []( book& b ) {
          b.a = 1;
          b.b = 2;
      } );

      db.modify( new_book, [&]( book& b ) {
          b.a = 3;
          b.b = 4;
      });

      {
         BOOST_TEST_MESSAGE( "Starting undo session" );
         auto session1 = db.start_undo_session(true);
         BOOST_REQUIRE_EQUAL( db.revision(), 43 );
         BOOST_REQUIRE_EQUAL( session1.revision(), 43 );
         BOOST_CHECK_THROW( db.set_revision( 13 ), std::logic_error ); /// Should not be able to change revision when the undo stack is not empty

         db.modify( new_book, [&]( book& b ) {
             b.a = 5;
             b.b = 6;
         });

         BOOST_REQUIRE_EQUAL( new_book.a, 5 );
         BOOST_REQUIRE_EQUAL( new_book.b, 6 );

         {
            BOOST_TEST_MESSAGE( "Starting undo session" );
            auto session2 = db.start_undo_session(true);

            db.modify( new_book, [&]( book& b ) {
                b.a = 7;
                b.b = 8;
            });

            BOOST_REQUIRE_EQUAL( db.revision(), 44 );
            BOOST_REQUIRE_EQUAL( session2.revision(), 44 );

            BOOST_TEST_MESSAGE( "Squashing latest undo session" );
            session2.squash();

            BOOST_REQUIRE_EQUAL( db.revision(), 43 ); /// Revision should have decreased because of the squash.
            BOOST_REQUIRE_EQUAL( session2.revision(), 44 ); /// But the revision of the session has not changed. Is this desired behavior? Or should the revision of the session decrement as well?

            BOOST_TEST_MESSAGE( "Allowing latest undo session to go out of scope" );
         }

         /// Despite session2 going out of scope, the revision has not changed and neither has the book object because we explicitly called squash on the session.
         BOOST_REQUIRE_EQUAL( db.revision(), 43 );
         BOOST_REQUIRE_EQUAL( new_book.a, 7 );
         BOOST_REQUIRE_EQUAL( new_book.b, 8 ); 

         BOOST_REQUIRE_EQUAL( session1.revision(), 43);
         decltype(session1) session(std::move(session1)); /// This should simply replace session1 with session but otherwise keep the same behavior.
         BOOST_REQUIRE_EQUAL( db.revision(), 43 );
         BOOST_REQUIRE_EQUAL( session.revision(), 43);

         BOOST_TEST_MESSAGE( "Allowing latest undo session to go out of scope" );
      }

      /// However, when session1 went out of scope, it automatically undid the head session (revision 43) which reverts the state of the book back to what it was set to prior to creating session1.
      BOOST_REQUIRE_EQUAL( db.revision(), 42 );
      BOOST_REQUIRE_EQUAL( new_book.a, 3 );
      BOOST_REQUIRE_EQUAL( new_book.b, 4 );
      
      decltype(db) db2(std::move(db));
      BOOST_REQUIRE_EQUAL( db2.revision(), 42 );

      {
         BOOST_TEST_MESSAGE( "Starting undo session" );
         auto session = db2.start_undo_session(true);

         BOOST_REQUIRE_EQUAL( session.revision(), 43 );

         db2.add_index< author_index >();

         BOOST_TEST_MESSAGE( "Creating author" );
         const auto& new_author = db2.create<author>( []( author& a ) {
            a.name = "Mark Twain";
            a.num_books = 13;
         });

         auto& bindx = db2.get_index<book_index>();
         BOOST_REQUIRE_EQUAL( bindx.revision(), 43 );

         auto& aindx = db2.get_mutable_index<author_index>();
         BOOST_REQUIRE_EQUAL( aindx.revision(), 43 ); /// Should have same revision as bindx even though their stack sizes are different

         /*
         aindx.set_revision(13); /// This is currently allowed (since the index has no undo sessions) even though it probably shouldn't be allowed. TODO: fix this.
         BOOST_REQUIRE_EQUAL( aindx.revision(), 13 );
         */

         {
            BOOST_TEST_MESSAGE( "Starting undo session" );
            auto session = db2.start_undo_session(true);

            BOOST_REQUIRE_EQUAL( db2.revision(), 44 );
            BOOST_REQUIRE_EQUAL( bindx.revision(), 44 );
            BOOST_REQUIRE_EQUAL( aindx.revision(), 44 );

            BOOST_CHECK_THROW( aindx.set_revision(13), std::logic_error );

            BOOST_TEST_MESSAGE( "Modifying author" );

            db2.create<author>( []( author& a ) {
               a.name = "F. Scott Fitzgerald";
               a.num_books = 13;
            });

            BOOST_REQUIRE_EQUAL( (db2.get<author, by_num_books, int>(13).name), "F. Scott Fitzgerald" );

            auto& aindx2 = db2.get_index<author_index, by_num_books>();
            BOOST_REQUIRE_EQUAL( aindx2.begin()->name, "F. Scott Fitzgerald" );

            db2.modify( new_author, [&]( author& a ) {
                a.num_books += 11;
            });

            BOOST_REQUIRE_EQUAL( aindx2.begin()->name, "Mark Twain" );

            BOOST_TEST_MESSAGE( "Pushing latest undo session" );
            session.push();

            BOOST_TEST_MESSAGE( "Allowing latest undo session to go out of scope" );
         }

         BOOST_REQUIRE_EQUAL( db2.revision(), 44 );

         BOOST_REQUIRE_EQUAL( (db2.get<author, by_num_books, int>(24).name), "Mark Twain" );

         BOOST_TEST_MESSAGE( "Committing up to and including the latest revision" );
         db2.commit(44);

         BOOST_REQUIRE_EQUAL( db2.revision(), 44 );

         BOOST_TEST_MESSAGE( "Allowing latest undo session to go out of scope" );
      }

      BOOST_REQUIRE_EQUAL( db2.revision(), 44 );

   } catch ( ... ) {
      bfs::remove_all( temp );
      throw;
   }
   bfs::remove_all( temp );
}


BOOST_AUTO_TEST_CASE( check_read_only ) {
   boost::filesystem::path temp = boost::filesystem::unique_path();
   try {
      namespace bfs = boost::filesystem;

      std::cerr << temp.native() << " \n";

      BOOST_CHECK_THROW( chainbase::database db(temp, database::read_only, 1024*1024*8), std::runtime_error );

      bfs::create_directories( temp );

      BOOST_CHECK_THROW( chainbase::database db(temp, database::read_only, 1024*1024*8), std::runtime_error );

      {
         chainbase::database db(temp, database::read_write, 1024*1024*8);

         BOOST_REQUIRE_EQUAL( db.is_read_only(), false );
      }

      chainbase::database db(temp, database::read_only, 1024*1024*8);
      BOOST_REQUIRE_EQUAL( db.is_read_only(), true );


   } catch ( ... ) {
      bfs::remove_all( temp );
      throw;
   }
   bfs::remove_all( temp );
}

// BOOST_AUTO_TEST_SUITE_END()
