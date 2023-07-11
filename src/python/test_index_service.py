import grpc
import logging

from src.proto import index_service_pb2
from src.proto import index_service_pb2_grpc


logger = logging.getLogger(__name__)


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    with grpc.insecure_channel('localhost:50051') as channel:
        stub = index_service_pb2_grpc.IndexServiceStub(channel)
        response = stub.Describe(index_service_pb2.DescribeRequest())

        logger.info(f"Describe: {response}.")

        response = stub.Search(
            index_service_pb2.SearchRequest(
                k=5,
                query_vector=[1],
            ),
        )

        logger.info(f"Empty search: {response}.")

        try:
            response = stub.Insert(
                index_service_pb2.InsertRequest(
                    vectors=[
                        index_service_pb2.Vector(id=id_ + 1, raw=raw)
                        for id_, raw in enumerate([[1], [2], [3]])
                    ]
                )
            )
            logger.info(f"Upsert: {response}.")
        except Exception as e:
            logger.info(f"Upsert exception: {e}")

        response = stub.Describe(index_service_pb2.DescribeRequest())

        logger.info(f"Describe: {response}.")

        response = stub.Search(
            index_service_pb2.SearchRequest(
                k=5,
                query_vector=[1],
            ),
        )

        logger.info(f"Search: {response}.")

        response = stub.Search(
            index_service_pb2.SearchRequest(
                k=5,
                query_vector=[1],
            ),
        )

        logger.info(f"Search: {response}.")

        try:
            response = stub.Upsert(
                index_service_pb2.UpsertRequest(
                    vectors=[
                        index_service_pb2.Vector(id=id_ + 1, raw=raw)
                        for id_, raw in enumerate([[1.1], [2.1], [3.1], [4.1]])
                    ]
                )
            )
            logger.info(f"Upsert: {response}.")
        except Exception as e:
            logger.info(f"Upsert exception: {e}")

        response = stub.Describe(index_service_pb2.DescribeRequest())

        logger.info(f"Describe: {response}.")

        response = stub.Search(
            index_service_pb2.SearchRequest(
                k=5,
                query_vector=[1],
            ),
        )

        logger.info(f"Search: {response}.")

